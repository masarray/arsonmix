// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
namespace armix::mixer {
// One mixer graph, two independent routing destinations. The direct output
// is preferred for minimal latency; any optional secondary playback device
// has separate clock/FIFO as appropriate and does not reprocess VST3.
struct LiveOutputRouteGate final {
    struct State {bool renderMain=false, hardware=false, secondary=false;};
    static constexpr State evaluate(bool routeAccepted,bool directAuthorized,
             bool normal,bool stable,bool masterUnmuted,
             bool hardwareArmed,bool secondaryArmed,
             bool nativeMainOnlyMute=false) noexcept {
        // Native MAIN mute controls MAIN PCM, not physical BUS/EXT authorization.
        // Actual global/device/epoch revocation still stops ALL outputs.
        const bool eligible=routeAccepted && directAuthorized && normal &&
                            stable && (masterUnmuted || nativeMainOnlyMute);
        return {eligible&&(hardwareArmed||secondaryArmed),
                eligible&&hardwareArmed,eligible&&secondaryArmed};
    }
};
// No extra graph, lock or FX pass: smooth just the MAIN output amplitude.
struct MainOnlyMuteSlew final {
    float gain=1.f;
    void reset(bool muted) noexcept {gain=muted?0.f:1.f;}
    float next(bool muted) noexcept {
        constexpr float step=1.f/64.f;
        gain=muted?std::max(0.f,gain-step):std::min(1.f,gain+step);
        return gain;
    }
};
}
