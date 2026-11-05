// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// P0 First Sound: CONTROL-thread source/OUT transaction policy.
// No realtime callback, locks, dynamic allocation or new DSP ownership.
namespace armix::mixer {
struct NativeTopologyEditSafety final {
    struct MixSnapshot final {
        float linear=1.f, pan=0.f;
        bool mute=false, solo=false;
    };
    // Before replacing an admitted ChannelId graph, copy its ACTUAL live
    // fader/pan/mute/solo into the next M4 topology. A native gesture must
    // never silently revert to an earlier fallback mixer value.
    static constexpr MixSnapshot preserveAcceptedMix(
        MixSnapshot legacy,MixSnapshot native,bool channelIdOwnsPcm) noexcept {
        return channelIdOwnsPcm?native:legacy;
    }
    static constexpr bool structuralSource(bool hasAcceptedRoute,
                                           bool sameFormat) noexcept {
        return !hasAcceptedRoute || !sameFormat;
    }
    static constexpr bool mayEdit(bool structural, bool streamArmed) noexcept {
        // An atomic physical pin switch does not replace a PCM graph.
        return !structural || !streamArmed;
    }
    static constexpr bool mayRestoreMonitor(bool previouslyArmed,
                                            bool sameDeviceEpoch,
                                            bool outputArmSafe,
                                            bool streamArmed) noexcept {
        return previouslyArmed && sameDeviceEpoch && outputArmSafe && !streamArmed;
    }
};
} // namespace armix::mixer
