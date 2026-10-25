// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
namespace armix::device
{
// Message-thread-only liveness observer over EXISTING atomic callback counters.
// It detects host callback silence, not backend-confirmed output xruns.
// No worker, allocation, clock read, OS call or added callback hot-path work.
class IndependentStreamWatchdog final
{
public:
    enum class Stall : std::uint8_t { none, capture, render, both };
    static constexpr std::uint64_t silenceThresholdMs = 3000;
    struct Snapshot
    {
        Stall stall = Stall::none;
        std::uint64_t captureSilentMs = 0, renderSilentMs = 0;
        bool armed = false;
    };
    void reset() noexcept
    {
        armed_ = false; latched_ = Stall::none;
        captureCount_ = renderCount_ = 0;
        lastCaptureAt_ = lastRenderAt_ = lastNow_ = 0;
    }
    Stall observe (std::uint64_t nowMs, std::uint64_t captureCount,
                   std::uint64_t renderCount) noexcept
    {
        if (!armed_)
        {
            armed_ = true;
            captureCount_ = captureCount; renderCount_ = renderCount;
            lastCaptureAt_ = lastRenderAt_ = lastNow_ = nowMs;
            return latched_;
        }
        if (nowMs < lastNow_) // defensive monotonic-clock regression
        {
            captureCount_ = captureCount; renderCount_ = renderCount;
            lastCaptureAt_ = lastRenderAt_ = lastNow_ = nowMs;
            return latched_;
        }
        lastNow_ = nowMs;
        if (captureCount != captureCount_)
            { captureCount_ = captureCount; lastCaptureAt_ = nowMs; }
        if (renderCount != renderCount_)
            { renderCount_ = renderCount; lastRenderAt_ = nowMs; }
        if (latched_ != Stall::none) return latched_;
        const bool captureMissing = nowMs - lastCaptureAt_ >= silenceThresholdMs;
        const bool renderMissing = nowMs - lastRenderAt_ >= silenceThresholdMs;
        latched_ = captureMissing && renderMissing ? Stall::both
                 : captureMissing ? Stall::capture
                 : renderMissing ? Stall::render : Stall::none;
        // Fault is latched until a controlled device close/reset.
        return latched_;
    }
    Snapshot snapshot() const noexcept
    {
        if (!armed_) return {};
        return {latched_, lastNow_ - lastCaptureAt_, lastNow_ - lastRenderAt_, true};
    }
    static const char* name (Stall fault) noexcept
    {
        switch (fault)
        {
            case Stall::capture: return "CAPTURE_CALLBACK_STALLED";
            case Stall::render: return "RENDER_CALLBACK_STALLED";
            case Stall::both: return "CAPTURE_AND_RENDER_CALLBACKS_STALLED";
            default: return "NONE";
        }
    }
private:
    std::uint64_t captureCount_ = 0, renderCount_ = 0;
    std::uint64_t lastCaptureAt_ = 0, lastRenderAt_ = 0, lastNow_ = 0;
    Stall latched_ = Stall::none;
    bool armed_ = false;
};
} // namespace armix::device
