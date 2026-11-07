// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

namespace armix::device {

// The mixer document is not a hardware binding. An epoch change invalidates
// PCM admission, NOT the operator's authored controls, WIREs or VST3 leases.
// All decisions happen on the JUCE message/control thread; no audio callback
// may read or mutate the retained document.
enum class DeviceSessionDecision : std::uint8_t {
    rejectedDevice,
    retainBoundMixer,
    createFirstSilentMixer,
    retainUnboundMixer
};

[[nodiscard]] constexpr DeviceSessionDecision decideDeviceSession(
    bool verifiedDevice, bool mixerReadyForCurrentEpoch,
    bool hasAuthoredMixerContent) noexcept
{
    if (!verifiedDevice) return DeviceSessionDecision::rejectedDevice;
    if (mixerReadyForCurrentEpoch) return DeviceSessionDecision::retainBoundMixer;
    return hasAuthoredMixerContent ? DeviceSessionDecision::retainUnboundMixer
                                 : DeviceSessionDecision::createFirstSilentMixer;
}

} // namespace armix::device
