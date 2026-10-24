// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include <memory>

// P1: single stereo microphone -> one VST3 -> master output.
// Structural mutations happen on the message thread ONLY after detaching the
// audio callback. No plugin load/scan/IO/mutex/logging/heap allocation by host
// code inside audioDeviceIOCallbackWithContext.
class LiveAudioEngine final : private juce::AudioIODeviceCallback
{
public:
    LiveAudioEngine()
    {
        formatManager_.addFormat (std::make_unique<juce::VST3PluginFormat>());
        scratch_.setSize (2, maxFrames, false, true, false);
    }

    ~LiveAudioEngine() override { stop(); }

    juce::AudioDeviceManager& devices() noexcept { return deviceManager_; }

    juce::String start()
    {
        // Disarmed on launch to prevent acoustic feedback with open microphones.
        monitorEnabled_.store (false);
        const auto error = deviceManager_.initialise (1, 2, nullptr, true);
        if (error.isNotEmpty())
            return error;
        deviceManager_.addAudioCallback (this);
        callbackAttached_ = true;
        return {};
    }

    void stop()
    {
        if (callbackAttached_)
        {
            deviceManager_.removeAudioCallback (this);
            callbackAttached_ = false;
        }
        if (plugin_ != nullptr)
        {
            plugin_->releaseResources();
            plugin_.reset();
        }
        deviceManager_.closeAudioDevice();
    }

    void setMonitorEnabled (bool enabled) noexcept { monitorEnabled_.store (enabled); }
    bool isMonitorEnabled() const noexcept { return monitorEnabled_.load(); }
    void setBypassed (bool bypassed) noexcept { bypassed_.store (bypassed); }
    bool isBypassed() const noexcept { return bypassed_.load(); }
    void setMasterGainDb (float db) noexcept
    {
        if (std::isfinite (db))
            linearGain_.store (std::pow (10.0f, juce::jlimit (-60.0f, 10.0f, db) / 20.0f));
    }

    // Message-thread operation. VST3 code is untrusted and may hang or crash;
    // sandboxed scanning/hosting is deliberately a later milestone.
    juce::String loadVst3 (const juce::File& path)
    {
        if (! path.exists() || ! path.hasFileExtension (".vst3"))
            return "Choose an installed .vst3 module or bundle";

        auto* format = formatManager_.getFormat (0);
        if (format == nullptr || ! format->fileMightContainThisPluginType (path.getFullPathName()))
            return "Not a recognised VST3 module";

        juce::OwnedArray<juce::PluginDescription> descriptions;
        format->findAllTypesForFile (descriptions, path.getFullPathName());
        if (descriptions.isEmpty())
            return "No VST3 effect found in the selected module";

        auto* device = deviceManager_.getCurrentAudioDevice();
        if (device == nullptr)
            return "Configure a working audio device first";

        juce::String error;
        auto candidate = formatManager_.createPluginInstance (
            *descriptions[0], device->getCurrentSampleRate(),
            juce::jmax (32, device->getCurrentBufferSizeSamples()), error);
        if (candidate == nullptr)
            return "VST3 load failed: " + error;

        juce::AudioProcessor::BusesLayout stereo;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        if (! candidate->setBusesLayout (stereo))
            return "P1 accepts stereo input/stereo output effects only";

        // Detach waits for in-flight callbacks, then reattach after publish.
        // The temporary silence is explicit P1 behaviour, not seamless hot swap.
        if (callbackAttached_)
            deviceManager_.removeAudioCallback (this);
        if (plugin_ != nullptr)
            plugin_->releaseResources();
        plugin_ = std::move (candidate);
        pluginName_ = descriptions[0]->name;
        bypassed_.store (false);
        if (callbackAttached_)
            deviceManager_.addAudioCallback (this);
        return {};
    }

    void unloadPlugin()
    {
        if (callbackAttached_)
            deviceManager_.removeAudioCallback (this);
        if (plugin_ != nullptr)
            plugin_->releaseResources();
        plugin_.reset();
        pluginName_.clear();
        if (callbackAttached_)
            deviceManager_.addAudioCallback (this);
    }

    juce::var status() const
    {
        auto object = std::make_unique<juce::DynamicObject>();
        auto* d = deviceManager_.getCurrentAudioDevice();
        object->setProperty ("connected", d != nullptr);
        object->setProperty ("driver", deviceManager_.getCurrentAudioDeviceType());
        object->setProperty ("device", d != nullptr ? d->getName() : "No audio device");
        object->setProperty ("rate", d != nullptr ? d->getCurrentSampleRate() : 0.0);
        object->setProperty ("buffer", d != nullptr ? d->getCurrentBufferSizeSamples() : 0);
        object->setProperty ("plugin", pluginName_);
        object->setProperty ("pluginLatencySamples", plugin_ != nullptr ? plugin_->getLatencySamples() : 0);
        object->setProperty ("monitor", monitorEnabled_.load());
        object->setProperty ("bypass", bypassed_.load());
        object->setProperty ("peak", peak_.load (std::memory_order_relaxed));
        object->setProperty ("note", "P1: input stereo -> optional VST3 -> output; CH1 + master only");
        return juce::var (object.release());
    }

private:
    static constexpr int maxFrames = 8192;
    juce::AudioDeviceManager deviceManager_;
    juce::AudioPluginFormatManager formatManager_;
    std::unique_ptr<juce::AudioPluginInstance> plugin_;
    juce::AudioBuffer<float> scratch_;
    juce::String pluginName_;
    bool callbackAttached_ = false;
    std::atomic<bool> monitorEnabled_ { false }, bypassed_ { false };
    std::atomic<float> linearGain_ { 1.0f }, peak_ { 0.0f };

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        if (plugin_ != nullptr)
        {
            plugin_->releaseResources();
            plugin_->prepareToPlay (device->getCurrentSampleRate(),
                                    juce::jmax (32, device->getCurrentBufferSizeSamples()));
        }
        peak_.store (0.0f, std::memory_order_relaxed);
    }

    void audioDeviceStopped() override { peak_.store (0.0f, std::memory_order_relaxed); }

    void audioDeviceIOCallbackWithContext (
        const float* const* inputs, int numInputs, float* const* outputs,
        int numOutputs, int samples,
        const juce::AudioIODeviceCallbackContext&) override
    {
        const bool enabled = monitorEnabled_.load (std::memory_order_relaxed);
        const bool bypass = bypassed_.load (std::memory_order_relaxed);
        const float gain = linearGain_.load (std::memory_order_relaxed);
        float peak = 0.0f;
        for (int offset = 0; offset < samples; offset += maxFrames)
        {
            const int frames = juce::jmin (maxFrames, samples - offset);
            // Scratch memory was allocated at startup: avoidReallocating=true.
            scratch_.setSize (2, frames, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* dst = scratch_.getWritePointer (ch);
                const float* src = numInputs > ch ? inputs[ch]
                                  : numInputs > 0 ? inputs[0] : nullptr;
                if (src != nullptr)
                    juce::FloatVectorOperations::copy (dst, src + offset, frames);
                else
                    juce::FloatVectorOperations::clear (dst, frames);
            }
            if (plugin_ != nullptr && ! bypass)
            {
                juce::MidiBuffer midi;
                plugin_->processBlock (scratch_, midi);
            }
            for (int ch = 0; ch < numOutputs; ++ch)
            {
                if (outputs[ch] == nullptr)
                    continue;
                float* out = outputs[ch] + offset;
                const float* source = scratch_.getReadPointer (juce::jmin (ch, 1));
                for (int i = 0; i < frames; ++i)
                {
                    const float x = enabled ? source[i] * gain : 0.0f;
                    out[i] = x;
                    peak = juce::jmax (peak, std::abs (x));
                }
            }
        }
        peak_.store (juce::jmin (1.0f, peak), std::memory_order_relaxed);
    }
};
