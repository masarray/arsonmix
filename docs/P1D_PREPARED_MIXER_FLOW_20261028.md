# ArSonMix P1D — PreparedMixerFlow execution contract (2026-10-28)

**Milestone:** compose P1A/P1B/P1C's static validated routing, sparse physical input mapping and bounded acyclic sends into an offline-executable dry mixer, while leaving the current JUCE native audio callback and user GUI COMPLETELY unchanged. Stacked on integration PR #112 `6d470cc07c05c87ce40ae954cdca35820a0ffdfd`.

## Existing baseline reconciled, no duplicate systems
- `ChannelRoutingPlan`: physical mono/stereo port identity, sparse callback indices, raw source extraction, format conversion.
- `ChannelSendPlan`: max 32 validated DAG edges, pre/post send taps, deterministic topological order, per-strip master inclusion.
- `Plan`: 10 source strips, gain/PAN/mute/solo and MASTER settings.
- Issue #15 remains the zero-plugin live hardware qualification gate; current native CH2–10 FX are not yet functional. PR #102 is still stale/conflicted and must not replace approved GUI code.

## New code
- `engine/core/PreparedMixerFlow.h` controls three verified subplans, compiles and validates all subplans before replacing the prepared result. Derives per-format gain coefficients on control thread (mono equal-power, stereo balance), including inputless bus returns. Prevents a physical-MONO-to-processing-STEREO channel from inheriting unwanted mono attenuation.
- `Workspace<Capacity>` is caller-owned **and preallocated**. Each incoming bus receives fixed L/R arrays; one scratch pair processes the current strip in topological order. No per-block malloc/locks/vector/sort/trigonometry. There is no device restart or audio callback mutation.
- `render` supports raw physical extraction; adds accumulated internal sends once; calls a **caller-provided per-strip FX hook** exactly between extraction/bus sum and fader; exposes pre-fader/post-FX sends and post-mute/solo/post-fader sends; routes selected strips to MASTER, then applies compiled MASTER mono/balance/gain.
- MUTE/SOLO policy: both suppress pre and post sends; zero-fader does **not** suppress pre-fader sends. This is one explicit engineering policy; alternate pre-mute taps require a future explicit selector, not implicit changes.
- Physically missing inputs fail closed **before** the VST processing hook when no internal source exists, avoiding fake output from a missing driver pin. A genuine internal FX return remains runnable with zero new source frames so reverberation tails can continue.
- If a requested block exceeds the preallocated capacity or invalid device input count, `render` returns false without memory writes. The eventual native host MUST clear its hardware output and emit a bounded offline error event on such failure.
- Dry FX hook accepts a stereo pair plus the explicitly selected processing format. The eventual VST3 host must keep per-strip plugin instances separate and validate mono/stereo bus layouts. No native VST instances are created here.

## Tests and acceptance
`tests/prepared_mixer_flow_test.cpp` verifies end-to-end: unassigned silence, inputless FX returns, pre-fader sends at -infinity dB, effects before pre-fader taps, post-fader gating, mute/solo including bus returns, 2-hop topology, sparse callback port identity, mono-source-to-stereo unity center, post-fader mono equal power, fanout/master summing, no stale block contents, transactional rollback on bad topology or physical ports, bounded frames, and missing input fail-closed behavior. `#undef NDEBUG` enables assertions in Release CTest.

Existing Windows workflow compiles `ARMixPreparedMixerFlowTests` with all prior Orbit/FxBypass/P1A/B/C tests, then runs CTest and packages native host. **No new GitHub workflow** is created.

## Remaining before any real live usage
1. Verify PR #112's exact-source CI green and reconcile current main. Merge main integration before retargeting this stacked PR; never merge child alone.
2. Control-thread `SourceRegistry` must map stable endpoint/port IDs to actual *enabled* callback pointer order. Present explicit source assignment in GUI only when endpoint is really available.
3. Create approved immutable graph revision publication with quiescent retirement, ACK and cancellation. Own preallocated channel scratch/master scratch outside the callback; never instantiate plugins inside callback or process one plugin object concurrently on two graphs.
4. Resolve #15 real 0-VST3 crackle first. Test output-only tone/capture-only/dry 48/44.1 kHz and concrete ASIO/WASAPI device+buffer matrix before enabling multichannel.
5. Per-channel DSP plugin ownership, routing snapshots, audio latencies/PDC, bus FX return and source identity, click-free mutation, meters and solo/mute cross-talk hardware acceptance. Do not claim all 10 channels live before those stages.

**Status:** pre-production engineering milestone, not a repaired crackle or 10-channel hardware-ready app.
