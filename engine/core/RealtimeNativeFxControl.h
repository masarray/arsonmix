// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Optional native FX control mailbox hook. The existing FxInstance invokes
// this on the SAME audio callback before its bypass fast path; the callback
// may drain one bounded edit even when an insert is fully bypassed.
// No allocation, lock, dynamic cast, UI or second renderer in this interface.
namespace armix::mixer {
struct RealtimeNativeFxControl {
    virtual ~RealtimeNativeFxControl() = default;
    virtual void consumeRealtimeEdits() noexcept = 0;
    // CONTROL-queried once when the native FX lease is prepared. Temporal DSP
    // that cannot resume from stale input (Harmony pitch/history/room) must
    // continue processing the actual PCM while its output is bypassed.
    // Ordinary EQ/compressor/vendor FX retain the CPU-saving bypass fast path.
    virtual bool requiresContinuousBypassProcessing() const noexcept {return false;}
    // AUDIO callback only. A native performer cue can be dangerous when
    // the rack is OFF: while fully bypassed its capture clock stops.
    // Default no-op preserves Compressor/EQ/Harmony and vendor FX behavior.
    virtual void onRealtimeBypass(bool /*requested*/,bool /*fully*/) noexcept {}

    // CONTROL-only lifecycle: after FxInstance accepts this processor, a
    // vendor-state restore must not mutate published audio-owned filters.
    virtual void sealForRealtime() noexcept {}
    // CONTROL-only Show capture guard: pending DSP edits must first be
    // consumed AND acknowledged, otherwise a saved state could be stale.
    virtual bool stateSnapshotQuiescent() const noexcept {return true;}
};
} // namespace armix::mixer
