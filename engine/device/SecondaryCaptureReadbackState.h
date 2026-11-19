// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

// IO-3A: portable, deterministic *CONTROL readback* of independently
// clocked B input capture. Driver-open is never equated with source LIVE;
// callback counts distinguish producer observation from actual primary
// consumer progress. No realtime work, allocation or device ownership.
namespace armix::device {
enum class SecondaryCaptureReadbackState {
    notConfigured, needsRemap, degraded, waitingForPcm, pcmFlowObserved
};
constexpr SecondaryCaptureReadbackState classifySecondaryCapture(
    bool installedMap, bool remapRequired, bool opened,
    bool faulted, bool primaryEpochMatches,
    std::uint64_t captureBlocks, std::uint64_t consumedBlocks) noexcept {
    if(remapRequired)return SecondaryCaptureReadbackState::needsRemap;
    if(!installedMap)return SecondaryCaptureReadbackState::notConfigured;
    if(!opened||faulted||!primaryEpochMatches)
        return SecondaryCaptureReadbackState::degraded;
    if(!captureBlocks||!consumedBlocks)
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
