// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace armix::device
{
// One immutable negotiated capture and render stream contract. PREPARE and
// observeStart() run only in device lifecycle/control code. The real-time
// callback checks channel COUNT only: O(1), no allocations, no device API.
// First fault is sticky until both callbacks have been detached and reset.
// Native endpoint IDs are independently verified by WasapiEndpointIdentityGuard;
// JUCE names alone never constitute hardware identity.
class IndependentStreamFormatFence final
{
public:
    enum class Side : std::uint8_t { capture, render };
    enum class Fault : std::uint8_t {
        none, invalidSelection, lifecycleIdentity, lifecycleRate,
        lifecyclePeriod, lifecycleChannelMap, callbackChannelCount
    };
    struct Endpoint
    {
        std::string name;
        double rateHz = 0.;
        int periodFrames = 0;
        std::string activeChannelMaskHex;
        int activeChannelCount = 0;
    };

    bool prepare(Endpoint capture, Endpoint render)
    {
        // The lifecycle caller has already fully stopped old callbacks.
        fault_.store(Fault::none, std::memory_order_release);
        capture_ = std::move(capture);
        render_ = std::move(render);
        if (!valid(capture_) || !valid(render_))
        {
            latch(Fault::invalidSelection);
            return false;
        }
        captureChannels_ = capture_.activeChannelCount;
        renderChannels_ = render_.activeChannelCount;
        return true;
    }

    bool observeStart(Side side, const Endpoint& actual) noexcept
    {
        const auto& expected = side == Side::capture ? capture_ : render_;
        if (fault() != Fault::none) return false;
        if (!valid(actual)) latch(Fault::invalidSelection);
        else if (actual.name != expected.name) latch(Fault::lifecycleIdentity);
        else if (std::abs(actual.rateHz - expected.rateHz) > 0.001)
            latch(Fault::lifecycleRate);
        else if (actual.periodFrames != expected.periodFrames)
            latch(Fault::lifecyclePeriod);
        else if (actual.activeChannelMaskHex != expected.activeChannelMaskHex ||
                 actual.activeChannelCount != expected.activeChannelCount)
            latch(Fault::lifecycleChannelMap);
        return fault() == Fault::none;
    }

    // Called ONLY on capture/render realtime thread: count is passed by JUCE,
    // never query JUCE AudioIODevice or allocate/format a channel mask here.
    bool observeCallbackChannels(Side side, int actualChannels) noexcept
    {
        if (fault() != Fault::none) return false;
        const int expected = side == Side::capture ? captureChannels_ : renderChannels_;
        if (actualChannels != expected)
        {
            latch(Fault::callbackChannelCount);
            return false;
        }
        return true;
    }

    Fault fault() const noexcept { return fault_.load(std::memory_order_acquire); }

    static const char* reason(Fault fault) noexcept
    {
        switch (fault)
        {
            case Fault::none: return "NONE";
            case Fault::invalidSelection: return "INVALID_NEGOTIATED_STREAM";
            case Fault::lifecycleIdentity: return "INDEPENDENT_STREAM_DEVICE_NAME_CHANGED";
            case Fault::lifecycleRate: return "INDEPENDENT_STREAM_SAMPLE_RATE_CHANGED";
            case Fault::lifecyclePeriod: return "INDEPENDENT_STREAM_BUFFER_PERIOD_CHANGED";
            case Fault::lifecycleChannelMap: return "INDEPENDENT_STREAM_CHANNEL_MASK_CHANGED";
            case Fault::callbackChannelCount: return "INDEPENDENT_CALLBACK_CHANNEL_COUNT_CHANGED";
        }
        return "UNKNOWN";
    }

    void resetStopped() noexcept
    {
        // Only after render and capture callbacks were REMOVED.
        fault_.store(Fault::none, std::memory_order_release);
        captureChannels_ = 0;
        renderChannels_ = 0;
        capture_ = {};
        render_ = {};
    }

private:
    static bool valid(const Endpoint& e) noexcept
    {
        if (e.name.empty() || !std::isfinite(e.rateHz) ||
            e.rateHz < 8000. || e.rateHz > 384000. ||
            e.periodFrames <= 0 || e.periodFrames > 65536 ||
            e.activeChannelCount < 1 || e.activeChannelCount > 32 ||
            e.activeChannelMaskHex.empty() || e.activeChannelMaskHex.size() > 1024)
            return false;
        int bits = 0;
        for (char ch : e.activeChannelMaskHex)
        {
            unsigned nibble = 0;
            if (ch >= '0' && ch <= '9') nibble = unsigned(ch - '0');
            else if (ch >= 'a' && ch <= 'f') nibble = unsigned(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') nibble = unsigned(ch - 'A' + 10);
            else return false;
            bits += std::popcount(nibble);
        }
        return bits == e.activeChannelCount;
    }

    void latch(Fault candidate) noexcept
    {
        Fault expected = Fault::none;
        fault_.compare_exchange_strong(expected, candidate,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed);
    }

    Endpoint capture_, render_; // immutable while callbacks run
    int captureChannels_ = 0, renderChannels_ = 0; // immutable while live
    std::atomic<Fault> fault_ {Fault::none};
};
} // namespace armix::device
