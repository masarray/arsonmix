// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
namespace armix::mixer {
// One mixer graph, two independent routing destinations. The direct output
// is preferred for minimal latency; any optional secondary playback device
// has separate clock/FIFO as appropriate and does not reprocess VST3.
struct LiveOutputRouteGate final {
    struct State {bool renderMain=false, hardware=false, secondary=false;};
    static constexpr State evaluate(bool routeAccepted,bool directAuthorized,
             bool normal,bool stable,bool masterUnmuted,
             bool hardwareArmed,bool secondaryArmed) noexcept {
        const bool eligible=routeAccepted && directAuthorized && normal &&
                            stable && masterUnmuted;
        return {eligible&&(hardwareArmed||secondaryArmed),
                eligible&&hardwareArmed,eligible&&secondaryArmed};
    }
};
}
