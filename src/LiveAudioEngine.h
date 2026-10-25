// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdint>
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
        // A mixer behaves like a signal path: ready I/O flows automatically.
        // The former MONITOR enable gate caused silent master/output confusion.
        // Panic OUTPUT MUTE remains available at all times.
        monitor_.store (true);
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
    // Emergency output mute, not a prerequisite for receiving audio.
    void setOutputMuted (bool mute) noexcept { monitor_.store (!mute, std::memory_order_release); }
    bool isOutputMuted() const noexcept { return !monitor_.load (std::memory_order_acquire); }
    void setDryTestEnabled (bool value) noexcept { dryTest_.store (value); }
    bool isDryTestEnabled() const noexcept { return dryTest_.load(); }
    void setMasterGainDb (float value) noexcept
    {
        if (std::isfinite (value)) masterGain_.store (juce::Decibels::decibelsToGain (juce::jlimit (-60.f, 10.f, value)));
    }
    void setChannelGainDb (float value) noexcept
    {
        if (std::isfinite (value)) channelGain_.store (juce::Decibels::decibelsToGain (juce::jlimit (-60.f, 10.f, value)));
    }
    // True stereo-to-stereo pan: centre preserves stereo; extremes fold
    // both stereo input channels to the chosen side at unity for dual-mono.
    // Parameter is normalized on the UI thread; callback only loads atomic.
    void setChannelPan (float value) noexcept
    {
        if (std::isfinite (value))
            channelPan_.store (juce::jlimit (-1.f, 1.f, value), std::memory_order_relaxed);
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
        o->setProperty ("channelMute", channelMute_.load());
        o->setProperty ("channelPan", channelPan_.load());
        o->setProperty ("dryTest", dryTest_.load());
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup (setup);
        o->setProperty ("inputDevice", setup.inputDeviceName);
        o->setProperty ("outputDevice", setup.outputDeviceName);
        o->setProperty ("deviceInputChannels", d ? d->getActiveInputChannels().countNumberOfSetBits() : 0);
        o->setProperty ("deviceOutputChannels", d ? d->getActiveOutputChannels().countNumberOfSetBits() : 0);
        o->setProperty ("callbackInputChannels", callbackInputs_.load());
        o->setProperty ("callbackOutputChannels", callbackOutputs_.load());
        o->setProperty ("callbackCount", static_cast<double> (callbackCount_.load()));
        o->setProperty ("callbackDurationUs", static_cast<double> (lastCallbackUs_.load()));
        o->setProperty ("worstCallbackUs", static_cast<double> (worstCallbackUs_.load()));
        o->setProperty ("slowCallbackCount", static_cast<double> (slowCallbacks_.load()));
        o->setProperty ("overBudgetCount", static_cast<double> (overBudgetCallbacks_.load()));
        o->setProperty ("callbackGapCount", static_cast<double> (callbackGaps_.load()));
        o->setProperty ("worstCallbackGapUs", static_cast<double> (worstCallbackGapUs_.load()));
        o->setProperty ("worstPluginUs", static_cast<double> (worstPluginUs_.load()));
        o->setProperty ("deviceStartCount", static_cast<double> (deviceStartCount_.load()));
        o->setProperty ("deviceStopCount", static_cast<double> (deviceStopCount_.load()));
        juce::Array<juce::var> pluginTimings;
        for (int i = 0; i < count_; ++i)
        {
            auto item = std::make_unique<juce::DynamicObject>();
            item->setProperty ("slot", i + 1);
            item->setProperty ("name", slots_[i].name);
            item->setProperty ("lastUs", static_cast<double> (lastPluginUsBySlot_[i].load()));
            item->setProperty ("worstUs", static_cast<double> (worstPluginUsBySlot_[i].load()));
            pluginTimings.add (juce::var (item.release()));
        }
        o->setProperty ("pluginTimings", juce::var (pluginTimings));
        o->setProperty ("slowPluginCount", static_cast<double> (slowPlugins_.load()));
        o->setProperty ("periodUs", d && d->getCurrentSampleRate() > 0
            ? 1000000.0 * d->getCurrentBufferSizeSamples() / d->getCurrentSampleRate() : 0.0);
        o->setProperty ("performanceNote", "Over-budget and callback-gap counts are timing indicators, not driver-confirmed xruns.");
        o->setProperty ("inputPeak", inputPeak_.load());
        o->setProperty ("postFxPeak", postFxPeak_.load());
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
    std::atomic<bool> monitor_ { false }, channelMute_ { false }, masterMute_ { false }, dryTest_ { false };
    std::atomic<float> masterGain_ { 1.f }, channelGain_ { 1.f }, channelPan_ { 0.f };
    std::atomic<float> inputPeak_ { 0.f }, postFxPeak_ { 0.f }, outputPeak_ { 0.f };
    std::atomic<int> callbackInputs_ { 0 }, callbackOutputs_ { 0 };
    std::atomic<unsigned long long> callbackCount_ { 0 };
    // Audio-thread-only timestamp and cheap atomically published counters.
    // No log writes, allocations, locks or WebView interactions in callback.
    std::atomic<std::uint64_t> lastCallbackUs_ {0}, worstCallbackUs_ {0};
    std::atomic<std::uint64_t> slowCallbacks_ {0}, overBudgetCallbacks_ {0};
    std::atomic<std::uint64_t> callbackGaps_ {0}, worstCallbackGapUs_ {0};
    std::atomic<std::uint64_t> worstPluginUs_ {0}, slowPlugins_ {0};
    std::atomic<std::uint64_t> deviceStartCount_ {0}, deviceStopCount_ {0};
    std::array<std::atomic<std::uint64_t>, maxFx> lastPluginUsBySlot_ {};
    std::array<std::atomic<std::uint64_t>, maxFx> worstPluginUsBySlot_ {};
    std::uint64_t previousCallbackStartUs_ = 0;
    double sampleRateHz_ = 48000.0;

    static std::uint64_t clockMicros() noexcept
    {
        return static_cast<std::uint64_t> (
            std::chrono::duration_cast<std::chrono::microseconds> (
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static void publishMax (std::atomic<std::uint64_t>& dst, std::uint64_t value) noexcept
    {
        auto old = dst.load (std::memory_order_relaxed);
        while (old < value && !dst.compare_exchange_weak (
            old, value, std::memory_order_relaxed)) {}
    }
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
        deviceStartCount_.fetch_add (1, std::memory_order_relaxed);
        sampleRateHz_ = juce::jmax (1.0, d->getCurrentSampleRate());
        for (int i = 0; i < maxFx; ++i)
        {
            lastPluginUsBySlot_[i].store (0, std::memory_order_relaxed);
            worstPluginUsBySlot_[i].store (0, std::memory_order_relaxed);
        }
        previousCallbackStartUs_ = 0;
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
        postFxPeak_.store (0.f);
        outputPeak_.store (0.f);
    }

    void audioDeviceStopped() override
    {
        deviceStopCount_.fetch_add (1, std::memory_order_relaxed);
        inputPeak_.store (0.f);
        outputPeak_.store (0.f);
    }

    void audioDeviceIOCallbackWithContext (
        const float* const* inputs, int inputChannels, float* const* outputs,
        int outputChannels, int samples, const juce::AudioIODeviceCallbackContext&) override
    {
        const std::uint64_t beginUs = clockMicros();
        const double periodUs = 1000000.0 * static_cast<double> (samples) / sampleRateHz_;
        // Scheduling gaps only suggest instability: pause/suspend and WASAPI
        // callback scheduling can create false positives. Never label xruns.
        if (previousCallbackStartUs_ != 0 && periodUs > 0)
        {
            const auto gap = beginUs - previousCallbackStartUs_;
            publishMax (worstCallbackGapUs_, gap);
            if (static_cast<double> (gap) > periodUs * 2.3)
                callbackGaps_.fetch_add (1, std::memory_order_relaxed);
        }
        previousCallbackStartUs_ = beginUs;
        callbackInputs_.store (inputChannels, std::memory_order_relaxed);
        callbackOutputs_.store (outputChannels, std::memory_order_relaxed);
        callbackCount_.fetch_add (1, std::memory_order_relaxed);
        const bool enabled = monitor_.load (std::memory_order_relaxed);
        const bool dry = dryTest_.load (std::memory_order_relaxed);
        const bool muted = channelMute_.load (std::memory_order_relaxed)
                        || masterMute_.load (std::memory_order_relaxed);
        const float gain = masterGain_.load (std::memory_order_relaxed)
                         * channelGain_.load (std::memory_order_relaxed);
        const float pan = channelPan_.load (std::memory_order_relaxed);
        // Precomputed stereo pan matrix; no trigonometry, locks or allocations
        // in the realtime sample loop. 0 = transparent identity matrix.
        const float ll = pan > 0.f ? 1.f - pan : 1.f + pan * .5f;
        const float lr = pan < 0.f ? -pan * .5f : 0.f;
        const float rl = pan > 0.f ? pan * .5f : 0.f;
        const float rr = pan < 0.f ? 1.f + pan : 1.f - pan * .5f;
        float inPeak = 0.f, postFxPeak = 0.f, outPeak = 0.f;
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
            if (!dry)
            {
                for (int fx = 0; fx < count_; ++fx)
                {
                    auto& slot = slots_[fx];
                    if (slot.processor && ! bypassFlags_[fx].load (std::memory_order_relaxed))
                    {
                        const auto pluginBeginUs = clockMicros();
                        slot.processor->processBlock (scratch_, midi);
                        const auto pluginUs = clockMicros() - pluginBeginUs;
                        lastPluginUsBySlot_[fx].store (pluginUs, std::memory_order_relaxed);
                        publishMax (worstPluginUsBySlot_[fx], pluginUs);
                        publishMax (worstPluginUs_, pluginUs);
                        if (static_cast<double> (pluginUs) > periodUs * 0.75)
                            slowPlugins_.fetch_add (1, std::memory_order_relaxed);
                    }
                    midi.clear();
                }
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* processed = scratch_.getReadPointer (ch);
                for (int i = 0; i < frames; ++i)
                    postFxPeak = juce::jmax (postFxPeak, std::abs (processed[i]));
            }
            for (int c = 0; c < outputChannels; ++c)
            {
                if (!outputs[c]) continue;
                const float* left = scratch_.getReadPointer (0);
                const float* right = scratch_.getReadPointer (1);
                float* dst = outputs[c] + offset;
                for (int i = 0; i < frames; ++i)
                {
                    const float mapped = c == 0 ? ll * left[i] + lr * right[i]
                                               : rl * left[i] + rr * right[i];
                    const float value = (enabled && !muted) ? mapped * gain : 0.f;
                    dst[i] = value;
                    outPeak = juce::jmax (outPeak, std::abs (value));
                }
            }
        }
        inputPeak_.store (juce::jmin (1.f, inPeak), std::memory_order_relaxed);
        postFxPeak_.store (juce::jmin (1.f, postFxPeak), std::memory_order_relaxed);
        outputPeak_.store (juce::jmin (1.f, outPeak), std::memory_order_relaxed);
        const auto elapsedUs = clockMicros() - beginUs;
        lastCallbackUs_.store (elapsedUs, std::memory_order_relaxed);
        publishMax (worstCallbackUs_, elapsedUs);
        if (static_cast<double> (elapsedUs) > periodUs * 0.80)
            slowCallbacks_.fetch_add (1, std::memory_order_relaxed);
        if (static_cast<double> (elapsedUs) > periodUs)
            overBudgetCallbacks_.fetch_add (1, std::memory_order_relaxed);
    }
};
