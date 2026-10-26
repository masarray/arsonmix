// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace armix::diagnostics
{
// MESSAGE/CONTROL THREAD ONLY: bounded PRE-MARK evidence over existing 1Hz
// status polls. No audio callback reads, locks, allocations, extra sampling,
// drivers, clocks, COM or log calls. A comparison NEVER crosses a JUCE
// device generation, route scope, counter reset, or a missing telemetry source.
class CrackleEvidenceWindow final
{
public:
    enum class Counter : std::size_t
    {
        renderedCallbacks,
        overBudgetCallbacks,
        lateCallbackIntervals,
        callbackGapIntervals,
        wasapiCaptureDiscontinuities,
        otherDriverXruns,
        bridgeUnderflowFrames,
        bridgePrimingFrames,
        bridgeOverflowFrames,
        bridgeNonFiniteInputSamples,
        bridgeCaptureCallbacks,
        bridgeRenderCallbacks,
        count
    };
    static constexpr std::size_t counterCount = static_cast<std::size_t>(Counter::count);
    struct Reading
    {
        std::uint64_t count = 0;
        bool available = false;
    };
    struct Sample
    {
        std::uint64_t atMs = 0;
        std::uint64_t deviceGeneration = 0;
        std::string routeScope; // stable backend, endpoints, mode and output gates
        std::array<Reading,counterCount> counters {};
        bool connected = false;
    };
    struct Delta
    {
        std::uint64_t value = 0;
        bool comparable = false; // unsupported/reset != observed zero
    };
    enum class Reason : std::uint8_t
    {
        ready, noDevice, insufficientHistory, changedRouteOrGeneration,
        stalePoll, clockRegression, noSampleInsideWindow
    };
    struct Window
    {
        Reason reason = Reason::insufficientHistory;
        std::uint64_t elapsedMs = 0;
        std::array<Delta,counterCount> changes {};
        bool ready() const noexcept { return reason == Reason::ready; }
    };

    static constexpr std::uint64_t lookbackMs = 5000;
    static constexpr std::uint64_t latestPollLimitMs = 1600;
    static constexpr std::uint64_t maximumPollIntervalMs = 2100;
    static constexpr std::size_t historyCapacity = 8;

    void push(Sample sample)
    {
        if (!sample.connected)
        {
            reset();
            return;
        }
        if (size_ != 0)
        {
            const auto& previous = recent();
            if (!sameScope(previous,sample) || sample.atMs <= previous.atMs
                || sample.atMs - previous.atMs > maximumPollIntervalMs)
                reset();
        }
        history_[next_] = std::move(sample);
        next_ = (next_ + 1) % historyCapacity;
        if (size_ < historyCapacity) ++size_;
    }

    Window mark(const Sample& now) const noexcept
    {
        Window result;
        if (!now.connected) { result.reason = Reason::noDevice; return result; }
        if (!size_) return result;
        const auto& latest = recent();
        if (!sameScope(latest,now))
        {
            result.reason = Reason::changedRouteOrGeneration;
            return result;
        }
        if (now.atMs < latest.atMs)
        {
            result.reason = Reason::clockRegression;
            return result;
        }
        if (now.atMs - latest.atMs > latestPollLimitMs)
        {
            result.reason = Reason::stalePoll;
            return result;
        }
        // The oldest eligible baseline is nearest to five seconds BEFORE the
        // user's mark. Do not interpolate or claim sub-second coincidence.
        // A two-sample baseline (<1s apart) is not meaningful evidence.
        const Sample* baseline = nullptr;
        for (std::size_t i=0; i<size_; ++i)
        {
            const auto& candidate = history_[(next_+historyCapacity-size_+i)%historyCapacity];
            if (candidate.atMs <= now.atMs && now.atMs-candidate.atMs <= lookbackMs
                && now.atMs-candidate.atMs >= 900)
            {
                baseline = &candidate;
                break;
            }
        }
        if (!baseline)
        {
            result.reason = Reason::noSampleInsideWindow;
            return result;
        }
        result.reason = Reason::ready;
        result.elapsedMs = now.atMs - baseline->atMs;
        for (std::size_t i=0; i<counterCount; ++i)
        {
            const auto& a = baseline->counters[i];
            const auto& b = now.counters[i];
            if (a.available && b.available && b.count >= a.count)
                result.changes[i] = {b.count-a.count,true};
        }
        return result;
    }
    void reset() noexcept { size_=next_=0; }

    static constexpr const char* name(Counter id) noexcept
    {
        switch(id)
        {
            case Counter::renderedCallbacks: return "HOST_CALLBACKS";
            case Counter::overBudgetCallbacks: return "HOST_DSP_OVER_BUDGET";
            case Counter::lateCallbackIntervals: return "HOST_LATE_START_1_5X";
            case Counter::callbackGapIntervals: return "HOST_LATE_START_2_3X";
            case Counter::wasapiCaptureDiscontinuities: return "WASAPI_CAPTURE_DISCONTINUITY_FLAGS";
            case Counter::otherDriverXruns: return "OTHER_BACKEND_DRIVER_DEFINED_XRUN";
            case Counter::bridgeUnderflowFrames: return "ASRC_INPUT_UNDERFLOW_FRAMES";
            case Counter::bridgePrimingFrames: return "ASRC_INPUT_PRIMING_SILENCE_FRAMES";
            case Counter::bridgeOverflowFrames: return "ASRC_CAPTURE_OVERFLOW_FRAMES";
            case Counter::bridgeNonFiniteInputSamples: return "ASRC_SANITIZED_NONFINITE_SAMPLES";
            case Counter::bridgeCaptureCallbacks: return "CAPTURE_CALLBACKS";
            case Counter::bridgeRenderCallbacks: return "RENDER_CALLBACKS";
            case Counter::count: break;
        }
        return "INVALID_COUNTER";
    }
    static constexpr const char* reasonName(Reason reason) noexcept
    {
        switch(reason)
        {
            case Reason::ready: return "BOUNDED_PRE_MARK_WINDOW";
            case Reason::noDevice: return "NO_CONNECTED_AUDIO_DEVICE";
            case Reason::insufficientHistory: return "NO_HISTORY_YET";
            case Reason::changedRouteOrGeneration: return "DEVICE_GENERATION_OR_ROUTE_CHANGED";
            case Reason::stalePoll: return "HISTORY_STALE";
            case Reason::clockRegression: return "MONOTONIC_CLOCK_REGRESSION";
            case Reason::noSampleInsideWindow: return "INSUFFICIENT_WINDOW_HISTORY";
        }
        return "INVALID_WINDOW";
    }
private:
    static bool sameScope(const Sample& a,const Sample& b) noexcept
    {
        return a.deviceGeneration == b.deviceGeneration &&
               a.routeScope == b.routeScope;
    }
    const Sample& recent() const noexcept
    {
        return history_[(next_+historyCapacity-1)%historyCapacity];
    }
    std::array<Sample,historyCapacity> history_ {};
    std::size_t next_ = 0, size_ = 0;
};
} // namespace armix::diagnostics
