// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "AdaptiveClockBridge.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace armix::device
{
// Separate capture and render JUCE AudioDeviceManagers: each owns a distinct
// WASAPI AudioIODevice and independently scheduled callback/endpoint clock.
// This is NOT two callbacks obtained from the existing joined duplex device.
// The render callback is the sole DSP/graph owner; capture only writes FIFO.
//
// All start()/close() calls and buffer allocations occur on the message thread
// while callbacks are STOPPED. The callback paths only read preallocated memory,
// update atomics and process DSP with the already-prepared engine callback.
//
// ASIO / genuinely same-clock devices should retain the original direct path.
class JuceIndependentClockDevice final
{
public:
    static constexpr int maxFrames = 8192;
    JuceIndependentClockDevice() : captureCallback_ (*this), renderCallback_ (*this)
    {
        staging_.setSize (2, maxFrames, false, true, false);
    }
    ~JuceIndependentClockDevice() { close(); }

    juce::String open (const juce::String& captureName,
                       const juce::String& renderName,
                       juce::AudioIODeviceCallback* downstream,
                       double targetLatencyMs = 24.)
    {
        close();
        if (! downstream || captureName.isEmpty() || renderName.isEmpty())
            return "Missing independent capture/render endpoints or DSP callback";
        if (! std::isfinite (targetLatencyMs) || targetLatencyMs < 5 || targetLatencyMs > 100)
            return "Unsupported clock-bridge target latency";
        downstream_ = downstream;
        const auto initialiseManager = [] (juce::AudioDeviceManager& manager,
                                           int inputs, int outputs,
                                           const juce::String& wanted,
                                           bool capture) -> juce::String
        {
            auto error = manager.initialise (inputs, outputs, nullptr, false);
            if (error.isNotEmpty()) return error;
            manager.setCurrentAudioDeviceType ("Windows Audio", false);
            if (manager.getCurrentAudioDeviceType() != "Windows Audio")
                return "Windows Audio shared device type unavailable";
            auto setup = manager.getAudioDeviceSetup();
            setup.inputDeviceName = capture ? wanted : juce::String();
            setup.outputDeviceName = capture ? juce::String() : wanted;
            setup.useDefaultInputChannels = capture;
            setup.useDefaultOutputChannels = !capture;
            setup.inputChannels.clear();
            setup.outputChannels.clear();
            error = manager.setAudioDeviceSetup (setup, false);
            if (error.isNotEmpty()) return error;
            auto* device = manager.getCurrentAudioDevice();
            if (!device) return "Windows audio device could not be opened";
            if (capture && (device->getActiveInputChannels().isZero()
                            || !device->getActiveOutputChannels().isZero()))
                return "Capture-only endpoint isolation rejected by driver";
            if (!capture && (device->getActiveOutputChannels().isZero()
                             || !device->getActiveInputChannels().isZero()))
                return "Render-only endpoint isolation rejected by driver";
            return {};
        };
        auto error = initialiseManager (captureManager_, 2, 0, captureName, true);
        if (error.isEmpty())
            error = initialiseManager (renderManager_, 0, 2, renderName, false);
        if (error.isNotEmpty())
        {
            close();
            return error;
        }

        auto* capture = captureManager_.getCurrentAudioDevice();
        auto* render = renderManager_.getCurrentAudioDevice();
        captureRate_ = capture->getCurrentSampleRate();
        renderRate_ = render->getCurrentSampleRate();
        captureBufferFrames_ = capture->getCurrentBufferSizeSamples();
        renderBufferFrames_ = render->getCurrentBufferSizeSamples();
        if (captureRate_ < 8000 || renderRate_ < 8000 ||
            captureBufferFrames_ <= 0 || renderBufferFrames_ <= 0)
        {
            close();
            return "Invalid negotiated sample rate or buffer frame count";
        }

        // 2 channels, 24 ms nominal bridge latency. Both rates are actual
        // negotiated endpoint rates, never hard-coded to 48 kHz.
        if (!bridge_.prepare (captureRate_, renderRate_, 2, 32768, targetLatencyMs))
        {
            close();
            return "Unable to allocate bounded capture FIFO / ASRC coefficients";
        }
        deviceFault_.store (false, std::memory_order_release);
        rateChanged_.store (false, std::memory_order_release);
        captureCallbacks_.store (0, std::memory_order_relaxed);
        renderCallbacks_.store (0, std::memory_order_relaxed);
        // Capture producer must start before the render-clock consumer.
        // Backends remain fully independent, never joined in one JUCE manager.
        captureManager_.addAudioCallback (&captureCallback_);
        captureAttached_ = true;
        active_.store (true, std::memory_order_release);
        renderManager_.addAudioCallback (&renderCallback_);
        renderAttached_ = true;
        return {};
    }

    void close()
    {
        active_.store (false, std::memory_order_release);
        if (renderAttached_)
        {
            renderManager_.removeAudioCallback (&renderCallback_);
            renderAttached_ = false;
        }
        if (captureAttached_)
        {
            captureManager_.removeAudioCallback (&captureCallback_);
            captureAttached_ = false;
        }
        renderManager_.closeAudioDevice();
        captureManager_.closeAudioDevice();
        downstream_ = nullptr;
    }

    bool active() const noexcept { return active_.load (std::memory_order_acquire); }
    bool faulted() const noexcept
    {
        return deviceFault_.load (std::memory_order_acquire)
            || rateChanged_.load (std::memory_order_acquire);
    }
    juce::AudioIODevice* captureDevice() const noexcept
    {
        return captureManager_.getCurrentAudioDevice();
    }
    juce::AudioIODevice* renderDevice() const noexcept
    {
        return renderManager_.getCurrentAudioDevice();
    }
    double captureRate() const noexcept { return captureRate_; }
    double renderRate() const noexcept { return renderRate_; }
    int captureBufferFrames() const noexcept { return captureBufferFrames_; }
    int renderBufferFrames() const noexcept { return renderBufferFrames_; }
    AdaptiveClockBridge::Status bridgeStatus() const noexcept { return bridge_.status(); }
    std::uint64_t captureCallbacks() const noexcept { return captureCallbacks_.load(); }
    std::uint64_t renderCallbacks() const noexcept { return renderCallbacks_.load(); }

private:
    struct Capture final : juce::AudioIODeviceCallback
    {
        JuceIndependentClockDevice& owner;
        explicit Capture (JuceIndependentClockDevice& p) : owner (p) {}
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            if (device && device->getCurrentSampleRate() != owner.captureRate_)
                owner.rateChanged_.store (true, std::memory_order_release);
        }
        void audioDeviceStopped() override
        {
            if (owner.active()) owner.deviceFault_.store (true, std::memory_order_release);
        }
        void audioDeviceError (const juce::String&) override
        {
            owner.deviceFault_.store (true, std::memory_order_release);
        }
        void audioDeviceIOCallbackWithContext (
            const float* const* inputs, int inputChannels,
            float* const* outputs, int outputChannels, int samples,
            const juce::AudioIODeviceCallbackContext&) override
        {
            for (int c=0; c<outputChannels; ++c)
                if (outputs[c]) juce::FloatVectorOperations::clear (outputs[c], samples);
            if (samples <= 0) return;
            owner.captureCallbacks_.fetch_add (1, std::memory_order_relaxed);
            owner.bridge_.push (inputs, inputChannels, samples);
        }
    };

    struct Render final : juce::AudioIODeviceCallback
    {
        JuceIndependentClockDevice& owner;
        explicit Render (JuceIndependentClockDevice& p) : owner (p) {}
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            if (device && device->getCurrentSampleRate() != owner.renderRate_)
                owner.rateChanged_.store (true, std::memory_order_release);
            if (owner.downstream_) owner.downstream_->audioDeviceAboutToStart (device);
        }
        void audioDeviceStopped() override
        {
            if (owner.downstream_) owner.downstream_->audioDeviceStopped();
            if (owner.active()) owner.deviceFault_.store (true, std::memory_order_release);
        }
        void audioDeviceError (const juce::String& message) override
        {
            owner.deviceFault_.store (true, std::memory_order_release);
            if (owner.downstream_) owner.downstream_->audioDeviceError (message);
        }
        void audioDeviceIOCallbackWithContext (
            const float* const*, int,
            float* const* outputs, int outputChannels, int samples,
            const juce::AudioIODeviceCallbackContext& context) override
        {
            if (samples <= 0) return;
            owner.renderCallbacks_.fetch_add (1, std::memory_order_relaxed);
            if (!owner.active() || !owner.downstream_ || owner.faulted())
            {
                for (int c=0; c<outputChannels; ++c)
                    if (outputs[c]) juce::FloatVectorOperations::clear (outputs[c], samples);
                return;
            }
            // Source input block is generated by ASRC using capture clock.
            // Forward the real render callback context with exact block size.
            // No dynamic vectors, blocking or buffer resize in render thread.
            for (int offset=0; offset<samples; offset += maxFrames)
            {
                const int frames = juce::jmin (maxFrames, samples-offset);
                auto* input = owner.staging_.getArrayOfWritePointers();
                owner.bridge_.pull (input, 2, frames);
                const float* stageRead[2] = {input[0],input[1]};
                std::array<float*, 32> outputViews {};
                for (int c=0; c<juce::jmin (outputChannels,32); ++c)
                    outputViews[static_cast<std::size_t>(c)] = outputs[c] ? outputs[c]+offset : nullptr;
                if (outputChannels > 32)
                {
                    owner.deviceFault_.store (true, std::memory_order_release);
                    for (int c=0; c<outputChannels; ++c)
                        if (outputs[c]) juce::FloatVectorOperations::clear (outputs[c]+offset, frames);
                    continue;
                }
                owner.downstream_->audioDeviceIOCallbackWithContext (
                    stageRead, 2, outputViews.data(), outputChannels, frames, context);
            }
        }
    };

    juce::AudioDeviceManager captureManager_, renderManager_;
    AdaptiveClockBridge bridge_;
    juce::AudioBuffer<float> staging_;
    juce::AudioIODeviceCallback* downstream_ = nullptr; // non-owning, externally synchronized
    Capture captureCallback_;
    Render renderCallback_;
    bool captureAttached_ = false, renderAttached_ = false; // control-thread only
    std::atomic<bool> active_ {false}, deviceFault_ {false}, rateChanged_ {false};
    std::atomic<std::uint64_t> captureCallbacks_ {0}, renderCallbacks_ {0};
    double captureRate_ = 0, renderRate_ = 0;
    int captureBufferFrames_ = 0, renderBufferFrames_ = 0;
};
} // namespace armix::device
