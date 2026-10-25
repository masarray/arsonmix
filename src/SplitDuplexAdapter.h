// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/device/AdaptiveClockBridge.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <chrono>
#include <cstdint>

// P0: real independent JUCE Windows Audio capture and render streams.
// This is deliberately NOT a second callback registered on the same manager:
// each AudioDeviceManager owns its own one-sided WASAPI AudioIODevice/clock.
// All lifecycle methods execute on the JUCE control/message thread. Both
// callbacks are removed and endpoints stopped before reconfiguring the bridge.
// No I/O, lock, malloc, COM query, or UI work in the producer callback.
namespace armix::device
{
class SplitDuplexAdapter final
{
    struct CaptureTap final : juce::AudioIODeviceCallback
    {
        explicit CaptureTap (AdaptiveClockBridge& bridge) : bridge_ (bridge) {}

        void audioDeviceIOCallbackWithContext (
            const float* const* input, int inputs,
            float* const*, int, int frames,
            const juce::AudioIODeviceCallbackContext&) override
        {
            const auto now = micros();
            lastCallbackUs.store (now, std::memory_order_relaxed);
            callbackCount.fetch_add (1, std::memory_order_relaxed);
            if (frames > 0 && inputs > 0 && input != nullptr)
                bridge_.push (input, inputs, frames);
        }
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            stopped_.store (false, std::memory_order_release);
            startCount.fetch_add (1, std::memory_order_relaxed);
            rateHz.store (device->getCurrentSampleRate(), std::memory_order_relaxed);
            lastCallbackUs.store (micros(), std::memory_order_relaxed);
        }
        void audioDeviceStopped() override
        {
            stopCount.fetch_add (1, std::memory_order_relaxed);
            // Publish stopped state for supervisory fallback even if no more
            // capture callbacks will ever arrive.
            stopped_.store (true, std::memory_order_release);
            lastCallbackUs.store (0, std::memory_order_relaxed);
        }
        static std::uint64_t micros() noexcept
        {
            return static_cast<std::uint64_t> (
                std::chrono::duration_cast<std::chrono::microseconds> (
                    std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        AdaptiveClockBridge& bridge_;
        std::atomic<std::uint64_t> lastCallbackUs {0}, callbackCount {0};
        std::atomic<std::uint64_t> startCount {0}, stopCount {0};
        std::atomic<double> rateHz {0.};
        std::atomic<bool> stopped_ {true};
    };

public:
    struct Readout
    {
        AdaptiveClockBridge::Status fifo;
        std::uint64_t captureCallbacks = 0;
        std::uint64_t captureAgeUs = 0;
        std::uint64_t captureStarts = 0, captureStops = 0;
        bool captureStopped = false;
        double captureRate = 0., outputRate = 0.;
        double preparedCaptureRate = 0., preparedRenderRate = 0.;
        int captureChannels = 0, renderChannels = 0;
        int capturePeriodFrames = 0, renderPeriodFrames = 0;
        int captureDriverXRuns = -1; // JUCE-backend-specific, not render underruns
    };

    SplitDuplexAdapter() : tap_ (bridge_) {}
    ~SplitDuplexAdapter() { stop(); }

    // First caller MUST stop the existing full-duplex device, preserving the
    // user's combined AudioDeviceSetup on the message thread for rollback.
    juce::String prepare (const juce::String& deviceType,
                          const juce::AudioDeviceManager::AudioDeviceSetup& original,
                          juce::AudioDeviceManager& renderManager,
                          double fifoTargetMs = 20.)
    {
        if (running_) return "Split stream already active";
        if (!deviceType.startsWithIgnoreCase ("Windows Audio")
            || deviceType.containsIgnoreCase ("Exclusive"))
            return "Separate-clock bridge is supported only on Windows Audio shared backends";
        if (original.inputDeviceName.isEmpty() || original.outputDeviceName.isEmpty())
            return "Both capture and render endpoints must be configured";

        juce::AudioDeviceManager::AudioDeviceSetup capture = original;
        capture.outputDeviceName.clear();
        capture.outputChannels.clear();
        capture.useDefaultOutputChannels = false;
        capture.useDefaultInputChannels = false;
        capture.inputChannels.clear();
        // Keep the working user's input mask: a mono device may reject asking
        // for a second physical channel that it does not expose.
        capture.inputChannels = original.inputChannels;
        if (capture.inputChannels.isZero()) capture.inputChannels.setBit (0);
        // Initialise with zero channels to avoid briefly opening a wrong default
        // endpoint. Then select the original backend and explicit capture name.
        auto error = captureManager_.initialise (0, 0, nullptr, false);
        if (error.isNotEmpty()) return "Capture manager initialise: " + error;
        captureManager_.setCurrentAudioDeviceType (deviceType, false);
        error = captureManager_.setAudioDeviceSetup (capture, false);
        if (error.isNotEmpty()) return "Independent capture open: " + error;

        auto* const cap = captureManager_.getCurrentAudioDevice();
        auto* const out = renderManager.getCurrentAudioDevice();
        if (cap == nullptr || out == nullptr)
            return "Two independent audio endpoints were not opened";
        const auto inputs = cap->getActiveInputChannels().countNumberOfSetBits();
        const auto outputs = out->getActiveOutputChannels().countNumberOfSetBits();
        if (inputs < 1 || outputs < 1
            || cap->getActiveOutputChannels().countNumberOfSetBits() != 0
            || out->getActiveInputChannels().countNumberOfSetBits() != 0)
            return "Backend refused independent capture/render isolation";

        const double captureRate = cap->getCurrentSampleRate();
        const double outputRate = out->getCurrentSampleRate();
        if (!bridge_.prepare (captureRate, outputRate, 2, 16384, fifoTargetMs))
            return "Cannot prepare a bounded drift-correction bridge at negotiated device rates";
        preparedCaptureRate_ = captureRate;
        preparedRenderRate_ = outputRate;
        // Strictly no callback can observe an unprepared or reset bridge.
        captureManager_.addAudioCallback (&tap_);
        captureAttached_ = true;
        running_ = true;
        return {};
    }

    void stop() noexcept
    {
        if (captureAttached_)
        {
            captureManager_.removeAudioCallback (&tap_);
            captureAttached_ = false;
        }
        captureManager_.closeAudioDevice();
        running_ = false;
    }

    bool running() const noexcept { return running_; }
    void pull (float* const* outputs, int channels, int frames) noexcept
    {
        bridge_.pull (outputs, channels, frames);
    }

    Readout snapshot (const juce::AudioIODevice* renderDevice) const
    {
        Readout r;
        r.fifo = bridge_.status();
        r.captureCallbacks = tap_.callbackCount.load (std::memory_order_relaxed);
        r.captureStarts = tap_.startCount.load (std::memory_order_relaxed);
        r.captureStops = tap_.stopCount.load (std::memory_order_relaxed);
        r.captureStopped = tap_.stopped_.load (std::memory_order_acquire);
        const auto last = tap_.lastCallbackUs.load (std::memory_order_relaxed);
        const auto now = CaptureTap::micros();
        r.captureAgeUs = last > 0 && now >= last ? now - last : 0;
        const auto* cap = captureManager_.getCurrentAudioDevice();
        r.captureRate = cap ? cap->getCurrentSampleRate() : 0.;
        r.outputRate = renderDevice ? renderDevice->getCurrentSampleRate() : 0.;
        r.preparedCaptureRate = preparedCaptureRate_;
        r.preparedRenderRate = preparedRenderRate_;
        const auto actualCallbackRate = tap_.rateHz.load (std::memory_order_relaxed);
        if (actualCallbackRate > 0.) r.captureRate = actualCallbackRate;
        r.captureChannels = cap ? cap->getActiveInputChannels().countNumberOfSetBits() : 0;
        r.renderChannels = renderDevice ? renderDevice->getActiveOutputChannels().countNumberOfSetBits() : 0;
        r.capturePeriodFrames = cap ? cap->getCurrentBufferSizeSamples() : 0;
        r.renderPeriodFrames = renderDevice ? renderDevice->getCurrentBufferSizeSamples() : 0;
        r.captureDriverXRuns = cap ? cap->getXRunCount() : -1;
        return r;
    }

private:
    AdaptiveClockBridge bridge_;
    juce::AudioDeviceManager captureManager_;
    CaptureTap tap_;
    double preparedCaptureRate_ = 0., preparedRenderRate_ = 0.; // message-thread-owned
    bool captureAttached_ = false, running_ = false; // message-thread only
};
} // namespace armix::device
