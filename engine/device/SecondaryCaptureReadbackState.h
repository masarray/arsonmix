// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
#include <cstdint>

// IO-3A: portable CONTROL-thread readback, never an audio renderer or a
// driver-open / source-audibility assertion. All timing is monotonic host time.
namespace armix::device {
enum class SecondaryCaptureReadbackState {
    notConfigured, needsRemap, degraded, waitingForPcm, pcmFlowObserved
};

// ASRC renderedFrames counts *all* output, including priming and starvation.
// Only frames with a usable FIR input window represent captured PCM.
// This is playback-time evidence, not proof of an audible ChannelId source.
constexpr std::uint64_t secondaryUsableAsrcFrames(
    std::uint64_t rendered, std::uint64_t priming,
    std::uint64_t underflow) noexcept {
    return rendered>priming && rendered-priming>underflow
        ? rendered-priming-underflow : 0;
}

// Off-RT stateful freshness guard. A single callback or an old cumulative
// counter cannot keep B labelled "PCM_FLOW_OBSERVED" indefinitely. Observe
// BOTH independent producer progress and genuinely usable ASRC output within
// the same bounded recent interval. Baseline the first observation for each
// native-issued device lease, including after reset or counters wrap.
class SecondaryCaptureFlowEvidence final {
public:
    static constexpr double freshnessMs=500.;
    std::uint64_t recentUsableFrames(
        std::uint64_t deviceLease, std::uint64_t captureBlocks,
        std::uint64_t rendered, std::uint64_t priming,
        std::uint64_t underflow, double nowMs,
        bool admitted) noexcept {
        const auto usable=secondaryUsableAsrcFrames(
            rendered,priming,underflow);
        if(!deviceLease||!std::isfinite(nowMs)||nowMs<0.) {
            reset();
            return 0;
        }
        if(deviceLease!=lease_ || captureBlocks<lastCaptureBlocks_ ||
           usable<lastUsableFrames_) {
            lease_=deviceLease;
            lastCaptureBlocks_=captureBlocks;
            lastUsableFrames_=usable;
            captureProgressMs_=-1.;
            usableProgressMs_=-1.;
            return 0;
        }
        if(!admitted) {
            lastCaptureBlocks_=captureBlocks;
            lastUsableFrames_=usable;
            captureProgressMs_=-1.;
            usableProgressMs_=-1.;
            return 0;
        }
        if(captureBlocks>lastCaptureBlocks_)captureProgressMs_=nowMs;
        if(usable>lastUsableFrames_)usableProgressMs_=nowMs;
        lastCaptureBlocks_=captureBlocks;
        lastUsableFrames_=usable;
        const bool freshCapture=captureProgressMs_>=0. &&
            nowMs>=captureProgressMs_ &&
            nowMs-captureProgressMs_<=freshnessMs;
        const bool freshUsable=usableProgressMs_>=0. &&
            nowMs>=usableProgressMs_ &&
            nowMs-usableProgressMs_<=freshnessMs;
        return freshCapture&&freshUsable?usable:0;
    }
    void reset() noexcept {
        lease_=lastCaptureBlocks_=lastUsableFrames_=0;
        captureProgressMs_=usableProgressMs_=-1.;
    }
private:
    std::uint64_t lease_=0,lastCaptureBlocks_=0,lastUsableFrames_=0;
    double captureProgressMs_=-1.,usableProgressMs_=-1.;
};

// The last parameter is RECENT usable ASRC frames, not authorized pull calls.
constexpr SecondaryCaptureReadbackState classifySecondaryCapture(
    bool installedMap, bool remapRequired, bool opened,
    bool faulted, bool primaryEpochMatches,
    std::uint64_t captureBlocks, std::uint64_t recentUsableFrames) noexcept {
    if(remapRequired)return SecondaryCaptureReadbackState::needsRemap;
    if(!installedMap)return SecondaryCaptureReadbackState::notConfigured;
    if(!opened||faulted||!primaryEpochMatches)
        return SecondaryCaptureReadbackState::degraded;
    if(!captureBlocks||!recentUsableFrames)
        return SecondaryCaptureReadbackState::waitingForPcm;
    return SecondaryCaptureReadbackState::pcmFlowObserved;
}
constexpr const char* secondaryCaptureStateName(
    SecondaryCaptureReadbackState state) noexcept {
    switch(state) {
    case SecondaryCaptureReadbackState::notConfigured:return "NOT_CONFIGURED";
    case SecondaryCaptureReadbackState::needsRemap:return "NEEDS_REMAP";
    case SecondaryCaptureReadbackState::degraded:return "DEGRADED";
    case SecondaryCaptureReadbackState::waitingForPcm:return "WAITING_FOR_PCM";
    case SecondaryCaptureReadbackState::pcmFlowObserved:return "PCM_FLOW_OBSERVED";
    }
    return "DEGRADED";
}
} // namespace armix::device
