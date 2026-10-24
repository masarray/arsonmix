// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

// One live stereo input path and up to 8 actual VST3 processors.
// The audio callback never scans files, creates plugins, mutates the chain,
// enters UI code, locks host mutexes, logs, or changes device settings.
// Chain edits detach audio callback first (brief intentional dropout in P2).
class LiveAudioEngine final : private juce::AudioIODeviceCallback
{
public:
    static constexpr int maxFx = 8;

    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::String name, catalogId;
        bool bypass = false;
        int latency = 0;
    };

    LiveAudioEngine()
    {
        formats_.addFormat (std::make_unique<juce::VST3PluginFormat>());
        scratch_.setSize (2, maxFrames, false, true, false);
    }

    ~LiveAudioEngine() override { stop(); }

    juce::AudioDeviceManager& devices() noexcept { return deviceManager_; }

    juce::String start (const juce::XmlElement* savedSettings = nullptr)
    {
        monitor_.store (false);
        const auto error = deviceManager_.initialise (1, 2, savedSettings, true);
        if (error.isNotEmpty()) return error;
        deviceManager_.addAudioCallback (this);
        attached_ = true;
        return {};
    }

    void stop()
    {
        detach();
        for (int i = 0; i < count_; ++i)
        {
            if (slots_[i].processor) slots_[i].processor->releaseResources();
            slots_[i] = Slot {};
        }
        count_ = 0;
        deviceManager_.closeAudioDevice();
    }

    void setMonitorEnabled (bool value) noexcept { monitor_.store (value); }
    bool isMonitorEnabled() const noexcept { return monitor_.load(); }
    void setMasterGainDb (float value) noexcept
    {
        if (std::isfinite (value)) masterGain_.store (juce::Decibels::decibelsToGain (juce::jlimit (-60.f, 10.f, value)));
    }
    void setChannelGainDb (float value) noexcept
    {
        if (std::isfinite (value)) channelGain_.store (juce::Decibels::decibelsToGain (juce::jlimit (-60.f, 10.f, value)));
    }
    void setChannelMuted (bool value) noexcept { channelMute_.store (value); }
    void setMasterMuted (bool value) noexcept { masterMute_.store (value); }

    int chainSize() const noexcept { return count_; }

    juce::AudioPluginInstance* pluginAt (int index) noexcept
    {
        return index >= 0 && index < count_ ? slots_[index].processor.get() : nullptr;
    }

    juce::var chainState() const
    {
        juce::Array<juce::var> nodes;
        for (int i = 0; i < count_; ++i)
        {
            auto node = std::make_unique<juce::DynamicObject>();
            node->setProperty ("index", i);
            node->setProperty ("name", slots_[i].name);
            node->setProperty ("id", slots_[i].catalogId);
            node->setProperty ("bypass", slots_[i].bypass);
            node->setProperty ("latency", slots_[i].latency);
            nodes.add (juce::var (node.release()));
        }
        return juce::var (nodes);
    }

    juce::String addVst3 (const juce::String& filename, int uid,
                          const juce::String& catalogId)
    {
        if (count_ >= maxFx) return "FX Rack is full (maximum 8 VST3 effects)";
        auto* device = deviceManager_.getCurrentAudioDevice();
        if (device == nullptr) return "Configure a working audio input/output device first";
        auto* fmt = formats_.getFormat (0);
        if (fmt == nullptr || ! fmt->fileMightContainThisPluginType (filename))
            return "VST3 module not found: " + filename;

        juce::OwnedArray<juce::PluginDescription> descs;
        fmt->findAllTypesForFile (descs, filename);
        juce::PluginDescription* chosen = nullptr;
        for (auto* d : descs)
            if (d != nullptr && d->uniqueId == uid && !d->isInstrument)
            {
                chosen = d;
                break;
            }
        if (chosen == nullptr)
            return "Effect not found inside VST3 module. Rescan the catalog.";

        juce::String error;
        auto instance = formats_.createPluginInstance (
            *chosen, device->getCurrentSampleRate(),
            juce::jmax (32, device->getCurrentBufferSizeSamples()), error);
        if (! instance)
            return "Failed to instantiate VST3: " + error;

        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        if (! instance->setBusesLayout (layout))
            return "P2 accepts stereo-in/stereo-out effects; plugin bus layout is incompatible";

        detach();
        Slot node;
        node.name = chosen->name;
        node.catalogId = catalogId;
        node.processor = std::move (instance);
        slots_[count_] = std::move (node);
        bypassFlags_[count_].store (false, std::memory_order_release);
        ++count_;
        attach();
        return {};
    }

    bool setBypass (int index, bool value)
    {
        if (index < 0 || index >= count_) return false;
        // Message-thread metadata and an atomic value consumed by the audio thread.
        slots_[index].bypass = value;
        bypassFlags_[index].store (value, std::memory_order_release);
        return true;
    }

    bool removeAt (int index)
    {
        if (index < 0 || index >= count_) return false;
        detach();
        if (slots_[index].processor)
            slots_[index].processor->releaseResources();
        for (int i = index; i + 1 < count_; ++i)
            slots_[i] = std::move (slots_[i + 1]);
        slots_[--count_] = Slot {};
        for (int i = 0; i < count_; ++i)
            bypassFlags_[i].store (slots_[i].bypass, std::memory_order_release);
        attach();
        return true;
    }

    bool moveSlot (int from, int to)
    {
        if (from < 0 || from >= count_ || to < 0 || to >= count_ || from == to)
            return false;
        detach();
        auto moved = std::move (slots_[from]);
        if (from < to)
            for (int i = from; i < to; ++i) slots_[i] = std::move (slots_[i + 1]);
        else
            for (int i = from; i > to; --i) slots_[i] = std::move (slots_[i - 1]);
        slots_[to] = std::move (moved);
        for (int i = 0; i < count_; ++i)
            bypassFlags_[i].store (slots_[i].bypass, std::memory_order_release);
        attach();
        return true;
    }

    juce::var status()
    {
        auto o = std::make_unique<juce::DynamicObject>();
        auto* d = deviceManager_.getCurrentAudioDevice();
        o->setProperty ("connected", d != nullptr);
        o->setProperty ("driver", deviceManager_.getCurrentAudioDeviceType());
        o->setProperty ("device", d ? d->getName() : "No device");
        o->setProperty ("rate", d ? d->getCurrentSampleRate() : 0.0);
        o->setProperty ("buffer", d ? d->getCurrentBufferSizeSamples() : 0);
        o->setProperty ("monitor", monitor_.load());
        o->setProperty ("masterMute", masterMute_.load());
        o->setProperty ("inputPeak", inputPeak_.load());
        o->setProperty ("peak", outputPeak_.load());
        o->setProperty ("latencySamples", totalLatency_.load());
        o->setProperty ("chainCount", count_);
        return juce::var (o.release());
    }

private:
    static constexpr int maxFrames = 8192;
    juce::AudioDeviceManager deviceManager_;
    juce::AudioPluginFormatManager formats_;
    std::array<Slot, maxFx> slots_;
    int count_ = 0;
    bool attached_ = false;
    juce::AudioBuffer<float> scratch_;
    // Indexed atomic bypass flags: instant RT-safe toggles, no graph detachment.
    std::array<std::atomic<bool>, maxFx> bypassFlags_ {};
    std::atomic<bool> monitor_ { false }, channelMute_ { false }, masterMute_ { false };
    std::atomic<float> masterGain_ { 1.f }, channelGain_ { 1.f };
    std::atomic<float> inputPeak_ { 0.f }, outputPeak_ { 0.f };
    std::atomic<int> totalLatency_ { 0 };

    void detach()
    {
        if (attached_)
        {
            deviceManager_.removeAudioCallback (this);
            attached_ = false;
        }
    }
    void attach()
    {
        if (! attached_ && deviceManager_.getCurrentAudioDevice() != nullptr)
        {
            deviceManager_.addAudioCallback (this);
            attached_ = true;
        }
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        int latency = 0;
        for (int i = 0; i < count_; ++i)
        {
            auto& slot = slots_[i];
            if (slot.processor)
            {
                slot.processor->releaseResources();
                slot.processor->prepareToPlay (d->getCurrentSampleRate(),
                    juce::jmax (32, d->getCurrentBufferSizeSamples()));
                slot.latency = slot.processor->getLatencySamples();
                if (!bypassFlags_[i].load (std::memory_order_relaxed)) latency += slot.latency;
            }
        }
        totalLatency_.store (latency);
        inputPeak_.store (0.f);
        outputPeak_.store (0.f);
    }

    void audioDeviceStopped() override
    {
        inputPeak_.store (0.f);
        outputPeak_.store (0.f);
    }

    void audioDeviceIOCallbackWithContext (
        const float* const* inputs, int inputChannels, float* const* outputs,
        int outputChannels, int samples, const juce::AudioIODeviceCallbackContext&) override
    {
        const bool enabled = monitor_.load (std::memory_order_relaxed);
        const bool muted = channelMute_.load (std::memory_order_relaxed)
                        || masterMute_.load (std::memory_order_relaxed);
        const float gain = masterGain_.load (std::memory_order_relaxed)
                         * channelGain_.load (std::memory_order_relaxed);
        float inPeak = 0.f, outPeak = 0.f;
        for (int offset = 0; offset < samples; offset += maxFrames)
        {
            const int frames = juce::jmin (maxFrames, samples - offset);
            scratch_.setSize (2, frames, false, false, true);
            for (int c = 0; c < 2; ++c)
            {
                float* dst = scratch_.getWritePointer (c);
                const float* src = inputChannels > c ? inputs[c]
                                  : inputChannels > 0 ? inputs[0] : nullptr;
                if (src)
                    juce::FloatVectorOperations::copy (dst, src + offset, frames);
                else
                    juce::FloatVectorOperations::clear (dst, frames);
                for (int i = 0; i < frames; ++i)
                    inPeak = juce::jmax (inPeak, std::abs (dst[i]));
            }
            // MIDI capacity is empty unless the plugin itself changes it.
            juce::MidiBuffer midi;
            for (int fx = 0; fx < count_; ++fx)
            {
                auto& slot = slots_[fx];
                if (slot.processor && ! bypassFlags_[fx].load (std::memory_order_relaxed))
                    slot.processor->processBlock (scratch_, midi);
                midi.clear();
            }
            for (int c = 0; c < outputChannels; ++c)
            {
                if (!outputs[c]) continue;
                const float* src = scratch_.getReadPointer (juce::jmin (c, 1));
                float* dst = outputs[c] + offset;
                for (int i = 0; i < frames; ++i)
                {
                    const float value = (enabled && !muted) ? src[i] * gain : 0.f;
                    dst[i] = value;
                    outPeak = juce::jmax (outPeak, std::abs (value));
                }
            }
        }
        inputPeak_.store (juce::jmin (1.f, inPeak), std::memory_order_relaxed);
        outputPeak_.store (juce::jmin (1.f, outPeak), std::memory_order_relaxed);
    }
};
