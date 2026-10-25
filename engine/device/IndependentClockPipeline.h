// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AdaptiveClockBridge.h"
#include "SplitStreamTelemetry.h"

namespace armix::device
{
// Concrete producer/consumer audio pipeline for *two independently clocked*
// callbacks. Ownership, device lifecycle and stream-format adaptation belong
// to a Windows backend adapter. This is intentionally JUCE-independent.
// prepare() only with both callbacks stopped; capture()/render() RT-safe.
class IndependentClockPipeline final
{
public:
    bool prepare (double captureHz, double renderHz, int channels,
                  int fifoFrames, double targetLatencyMs)
    {
        if (!bridge_.prepare(captureHz,renderHz,channels,fifoFrames,targetLatencyMs))
            return false;
        telemetry_.sourceStarted();
        telemetry_.renderStarted();
        return true;
    }
    int capture (const float* const* input,int channels,int frames) noexcept
    {
        const int written=bridge_.push(input,channels,frames);
        telemetry_.captured(frames,written);
        return written;
    }
    void render (float* const* output,int channels,int frames) noexcept
    {
        const auto before=bridge_.status().underflowFrames;
        bridge_.pull(output,channels,frames);
        const auto after=bridge_.status().underflowFrames;
        const auto delta=after>=before?after-before:0;
        telemetry_.rendered(frames,static_cast<int>(
            std::min<std::uint64_t>(delta,frames>0?static_cast<std::uint64_t>(frames):0)));
    }
    AdaptiveClockBridge::Status bridgeStatus() const noexcept
    {
        return bridge_.status();
    }
    SplitStreamSnapshot streamStatus() const noexcept
    {
        return telemetry_.snapshot();
    }
private:
    AdaptiveClockBridge bridge_;
    SplitStreamTelemetry telemetry_;
};
} // namespace armix::device
