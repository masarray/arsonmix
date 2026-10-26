// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

namespace armix::device
{
// CONTROL THREAD ONLY. Observes existing atomic ASRC counters at the control
// timer; this is NOT a device-driver render-xrun or physical latency detector.
// No worker, dynamic memory, OS call, callback mutation or blocking wait.
class BridgeContinuityGate final
{
public:
    enum class Fault : std::uint8_t { none, underflow, priming, overflow };
    struct Counters
    {
        std::uint64_t rendered = 0, underflow = 0, priming = 0, overflow = 0;
    };
    struct Snapshot
    {
        Fault fault = Fault::none;
        Fault lastWindowFault = Fault::none;
        unsigned consecutiveBadWindows = 0;
        unsigned lossBasisPoints = 0;
        std::uint64_t windowFrames = 0;
        bool armed = false;
    };
    static constexpr std::uint64_t warmupMs = 1500;
    static constexpr std::uint64_t windowMs = 1000;
    static constexpr unsigned requiredBadWindows = 3;

    void reset() noexcept
    {
        armed_ = false;
        fault_ = lastWindowFault_ = Fault::none;
        badWindows_ = 0;
        lossBasisPoints_ = 0;
        windowFrames_ = 0;
        armedAt_ = windowAt_ = lastNow_ = 0;
        baseline_ = {};
    }
    Fault observe (std::uint64_t nowMs, Counters current) noexcept
    {
        if (!armed_)
        {
            arm (nowMs, current);
            return fault_;
        }
        // Counter/time regressions represent a new observation generation,
        // not a huge synthetic underflow. Only control thread invokes us.
        if (nowMs < lastNow_ || current.rendered < baseline_.rendered
            || current.underflow < baseline_.underflow
            || current.priming < baseline_.priming
            || current.overflow < baseline_.overflow)
        {
            reset();
            arm (nowMs, current);
            return fault_;
        }
        lastNow_ = nowMs;
        if (fault_ != Fault::none) return fault_; // sticky until close/reset
        if (nowMs - armedAt_ < warmupMs)
        {
            baseline_ = current;
            windowAt_ = nowMs;
            return Fault::none;
        }
        if (nowMs - windowAt_ < windowMs) return Fault::none;

        const auto frames = current.rendered - baseline_.rendered;
        const auto missing = current.underflow - baseline_.underflow;
        const auto startup = current.priming - baseline_.priming;
        const auto discarded = current.overflow - baseline_.overflow;
        baseline_ = current;
        windowAt_ = nowMs;
        windowFrames_ = frames;
        // The separate callback watchdog owns the no-render-progress case.
        if (frames < 2048)
        {
            badWindows_ = 0;
            lastWindowFault_ = Fault::none;
            lossBasisPoints_ = 0;
            return Fault::none;
        }

        Fault observed = Fault::none;
        std::uint64_t affected = 0;
        // After the warmup window, BOTH underflow and repeated re-priming
        // mean absent audio PCM. AdaptiveClockBridge::pull() counts just the
        // first starved sample as underflow and the subsequent silence as
        // priming. Evaluating them separately would miss repeated crackles.
        // Use overflow-safe threshold arithmetic even for malformed counters.
        const auto starvation = missing > UINT64_MAX - startup
            ? UINT64_MAX : missing + startup;
        if (starvation > frames / 100)
        {
            observed = missing >= startup && missing != 0
                ? Fault::underflow : Fault::priming;
            affected = starvation;
        }
        else if (discarded > frames / 100)
            { observed = Fault::overflow; affected = discarded; }
        if (observed == Fault::none)
        {
            badWindows_ = 0;
            lastWindowFault_ = Fault::none;
            lossBasisPoints_ = 0;
            return Fault::none;
        }
        // "underflow" and "priming" are two counter views of the SAME
        // absent capture PCM. Do not reset the bad-window streak as the
        // bridge switches from missing samples into FIR re-priming.
        const bool sourceNow = observed == Fault::underflow || observed == Fault::priming;
        const bool sourceBefore = lastWindowFault_ == Fault::underflow
                               || lastWindowFault_ == Fault::priming;
        badWindows_ = (observed == lastWindowFault_ ||
                      (sourceNow && sourceBefore)) ? badWindows_ + 1 : 1;
        lastWindowFault_ = observed;
        // Control thread uses double to avoid uint64 multiply overflow.
        const double bp = 10000.0 * static_cast<double> (affected)
                          / static_cast<double> (frames);
        lossBasisPoints_ = bp >= 10000.0 ? 10000u
                           : static_cast<unsigned> (bp);
        if (badWindows_ >= requiredBadWindows) fault_ = observed;
        return fault_;
    }
    Snapshot snapshot() const noexcept
    {
        return {fault_, lastWindowFault_, badWindows_, lossBasisPoints_,
                windowFrames_, armed_};
    }
    static const char* name (Fault f) noexcept
    {
        switch (f)
        {
            case Fault::underflow: return "SUSTAINED_ASRC_UNDERFLOW";
            case Fault::priming: return "SUSTAINED_ASRC_PRIMING";
            case Fault::overflow: return "SUSTAINED_ASRC_OVERFLOW";
            default: return "NONE";
        }
    }
private:
    void arm (std::uint64_t nowMs, Counters current) noexcept
    {
        armed_ = true;
        armedAt_ = windowAt_ = lastNow_ = nowMs;
        baseline_ = current;
    }
    Counters baseline_ {};
    std::uint64_t armedAt_ = 0, windowAt_ = 0, lastNow_ = 0;
    std::uint64_t windowFrames_ = 0;
    Fault fault_ = Fault::none, lastWindowFault_ = Fault::none;
    unsigned badWindows_ = 0, lossBasisPoints_ = 0;
    bool armed_ = false;
};
} // namespace armix::device
