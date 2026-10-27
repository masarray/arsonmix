# ArSonMix P1A — verified foundation for real 10-channel routing + MASTER (2026-10-28)

Baseline: `main@de683b001a5a1e5e050bc20ddb52d32b22fa58d1`. Scope: **pure DSP core plus deterministic C++ tests, no live activation**. Superseded/stale UI-DSP PRs must not be bulk merged. Owner's global priority remains stable ultra-low-latency Windows audio and the approved v0.8 GUI; this is not a release or a hardware certification.

## Why this milestone exists

The current JUCE native callback (`src/LiveAudioEngine.h`) processes one stereo input source through one VST3 chain to the master; `src/Main.cpp` currently consumes channel fader/pan/mute only for Channel 1. Its ten other GUI channel/MASTER insert surfaces cannot be called native live FX until a real input/bus topology exists. Issue #15 **0-FX hardware crackle** remains a release gate, and the Smart Audio Engine ARD explicitly prohibits activating P2.6 live multi-channel DSP until that hardware gate passes.

## Implemented: device-independent, offline-prepared routing contract

`engine/core/MixerRoutingPlan.h`:

- Exactly 10 source strips + one MASTER. Every source starts **disconnected** (never invented or assigned to an arbitrary device pin). Physical input indexes are explicit; mono/stereo slots do not infer a nonexistent stereo device.
- The control thread validates *all* channel mappings, valid numeric parameters, stereo channel separation, source mode and physically negotiated input count. It compiles a temporary plan and atomically **as a unit at the API boundary** replaces the output Plan only on complete success. The caller must still publish the Plan across threads safely; **this implementation does not yet provide a graph swap or cross-thread publication**.
- Precomputed equal-power mono pan and stereo-balance coefficients; logical SOLO/MUTE arbitration; fader level, MASTER gain/PAN/mute/mono. No per-sample trig, heap, logging, scans or device operations.
- `extract(strip,...)` reads true physical channel input into preallocated stereo scratch, preserving stereo. `accumulate(strip,...)` sums post-FX stereo scratch into the MASTER mix. `finishMaster(...)` applies MASTER controls after the future master FX chain.
- Unassigned or unavailable physical input returns silence and `false` from `extract`; it never masquerades as a live VST3 or fabricates VU levels.

## Engineering invariants / tests

`tests/mixer_routing_plan_test.cpp` covers 10 independent mono sources, separate left/right physical inputs, stereo identity, mono centered -3 dB law, panic/mute/solo correctness, M/S output summing, invalid physical input and duplicate L/R index, non-finite pan, gain bounds, unknown source modes, recompile rollback and null/insufficient input availability. Existing Windows CTest builds `ARMixMixerRoutingTests` as part of the established workflow (no extra workflow or redundant matrix).

## Next integration gates (deliberately not marked complete)

1. Issue #15 must be physically gated for **0 FX** (OUTPUT_ONLY_TONE, CAPTURE_ONLY, DRY_THROUGH; negotiated driver rate/period and capture/renderer evidence). A CTest pass is not a substitute.
2. Create `SourceRegistry` populated from *actual* ASIO/WASAPI/virtual input endpoints, mapping by explicit per-channel source ID, not hard-coded GUI label or default fake input. Preserve robust DirectFence/independent clocks.
3. Allocate fixed scratch/master buffers ahead of callback; each source: `extract → channel VST chain → fader/pan/mute/solo accumulate`, then `MASTER FX → finishMaster → actual output`. Avoid feedback paths for FX Return. Handle mono/stereo VST bus layout and channel-local plugin/editor/Orbit identity.
4. Publish immutable source/routing snapshots at audio block boundary using the existing `LiveGraphHandoff` epoch/quiescent retirement pattern. No graph destruction, initialization, allocations, locks or device restarts on callback. Support transaction rollback, ACK and source availability status.
5. Verify per-source meters against true post-FX levels; add callback budget profiling, cross-channel isolation, device hotplug, recall, latency/PDC and minimum-buffer soak tests. No guaranteed click-free VST order change until measured on the real device.
6. Only then enable `ADD FX` for CH2–10 and MASTER in native GUI. An unrouted channel must show a truthful disabled/unassigned status until activation.

No installer/runtime changes are included in this milestone. Public/private binary feature parity and the portable VST Vault rules remain unchanged.
