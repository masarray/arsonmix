// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
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

// Retained processors stay on their prepared rate. A changed buffer is
// safe when every callback chunk fits both the old graph and the new device.
[[nodiscard]] constexpr int retainedMixerQuantum(
    int preparedFrames,int currentFrames,int maximumFrames,
    bool rateChanged,bool hasPreparedEffects) noexcept
{
    if(preparedFrames<=0 || currentFrames<=0 ||
       preparedFrames>maximumFrames || currentFrames>maximumFrames ||
       (rateChanged && hasPreparedEffects))return 0;
    return std::min(preparedFrames,currentFrames);
}

} // namespace armix::device
