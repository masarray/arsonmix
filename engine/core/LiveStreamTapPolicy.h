// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
namespace armix::mixer {
// STREAM1: ASIO/WASAPI direct mixer remains the sole DSP owner. These
// booleans gate the actual MAIN render once, then independent destinations.
// Explicit stream ARM does not unmute physical monitor; device reconfig,
// master mute and non-NORMAL diagnostic state silence BOTH destinations.
struct LiveStreamTapPolicy final {
    struct Gate {
        bool renderMain=false;
        bool hardware=false;
        bool stream=false;
    };
    static constexpr Gate evaluate(bool nativeRoute,bool directAuthorized,
        bool normal,bool stable,bool masterUnmuted,
        bool hardwareArmed,bool streamArmed) noexcept {
        const bool valid=nativeRoute && directAuthorized &&
                         normal && stable && masterUnmuted;
        const bool hardware=valid && hardwareArmed;
        const bool stream=valid && streamArmed;
        return {hardware||stream,hardware,stream};
    }
};
}
