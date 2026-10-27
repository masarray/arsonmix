# P1B — Independent mono/stereo strips and sparse physical-port contract

**Date:** 2026-10-28  
**Parent:** [P1A real 10-strip routing PR #108](https://github.com/masarray/arsonmix/pull/108)  
**Product authority:** [Routing and mono/stereo ARD #109](https://github.com/masarray/arsonmix/issues/109)  
**Safety boundaries:** [P0 0-VST crackle #15](https://github.com/masarray/arsonmix/issues/15), [FX reorder #102](https://github.com/masarray/arsonmix/pull/102), [portable performance #85](https://github.com/masarray/arsonmix/issues/85)

## Scope, not promotional claims

P1B extends the **control-thread-compiled, device-independent P1A DSP primitive**. It does **not** open ASIO/WASAPI, replace `LiveAudioEngine.h`, enable audio inputs for CH2–10, change the native WebView2 GUI, make sends live, or perform runtime VST3 mono/stereo layout negotiation. No Windows hardware FAT or live crackle guarantee is claimed.

The new `engine/core/ChannelRoutingContract.h` adds:

1. **Three independent channel-format choices:** `followSource` (default compatibility), `mono`, and `stereo`. A source can independently be `disconnected/mono/stereo`. These typed selections avoid the common error of treating source width and channel processing width as the same property.
2. **Device callback port mapping:** an explicitly provided list of actual enabled *physical ports in callback-pointer order*. `InputPortMap` verifies every port index is within the implementation boundary 0–255, has no duplicates, and maps physical port ID to dense callback pointer index. **This list must come from the opened device, not inferred from channel labels.** JUCE callback pointer ordering and each device's active bitsets need implementation/proof in the forthcoming adapter.
3. **Sparse/nonadjacent input choices:** physically numbered port 2 may be callback[1], and stereo L=5, R=7 may be callback[0] and callback[2]. Any unavailable physical port fails closed; existing routing plan is retained. The same hardware input may feed several independent mixer strips.
4. **Format conversion:** stereo → mono input folds down `0.5*(L+R)` before channel FX scratch; mono → stereo is explicitly dual mono, with calibrated -3 dB center send gain matching the original P1A mono center. The stereo→mono path uses a mono equal-power panner and the ordinary stereo→stereo path retains L/R balance.
5. **Prepared-only parameters:** format-dependent pan coefficients and physical mapping are compiled before audio RT. `extract` and `accumulate` are fixed-size, allocation-free operations on caller-owned preallocated buffers. No cache lookup, lock, device query, resize, VST construction, event emit, or thread spawning in callback.

## Important DSP rules for integration

- This P1B implementation models **channel format before FX**, not the plugin bus layout. Stereo reverb on a mono track may legitimately turn a mono-input DSP channel into a stereo-output FX path. Do not downmix the post-FX wet signal without explicit routing/policy. The actual negotiated plugin input/output bus layout (mono, stereo, mono-in/stereo-out) belongs to the next FX adapter gate.
- A mono source duplicated to stereo is *dual mono*, not claimed stereophonic width. A regular stereo source is not accidentally collapsed. The chosen -3 dB center mapping is a deliberate compatibility law, **not** a universal acoustical loudness guarantee when stereo plugins introduce decorrelated components.
- The simple `0.5*(L+R)` downmix can cancel anti-phase material. Show a phase/correlation warning in the eventual UI when appropriate; do not add a hidden signal-level maximizer.
- Legacy P1A `Plan` is composed, not rewritten or forked. Existing P1A default settings retain their behavior. There is no duplicate audio engine.
- The existing caller must publish validated routing plans via an explicit RT-safe ownership/epoch protocol. **P1B's all-or-nothing compile does not itself implement a cross-thread graph swap.** Device generation, graph generation, UI request/ACK and no stale update are next-stage requirements.
- Raw device-scoped physical indices are not portable stable identities. Workspace restore must check endpoint identity + per-port identity/label against the current driver, present missing/unmapped status and require user confirmation before live activation. Never silently reassign CH1 mic or monitor speaker.
- If the requested input port is outside the driver's current **active mask**, enabling it may need explicit Audio Setup and a device restart. Switching between already enabled ports can be compiled without reopening, once safe graph publication exists. Do not claim universally click-free switching.

## Tests in existing Windows CTest

`tests/channel_routing_contract_test.cpp` checks: sparse callback pointer order, nonadjacent stereo L/R, independent mono/stereo output coefficients, stereo-to-mono fold, center pan level continuity, invalid/missing ports, duplicate enabled port, invalid format, rejection without partial mutation, cross-strip input fanout, and corrupt NaN parameters.

Both `channel_routing_contract_test.cpp` and the inherited `mixer_routing_plan_test.cpp` explicitly restore assertions if `NDEBUG` is defined. This is important: native Windows Release CI otherwise compiles `assert()` away, accidentally reporting a test PASS without executing assertions.

New CTest target: `MonoStereoSparsePhysicalPortRouting`. No separate CI workflow. Local GCC 20 optimization with AddressSanitizer/UndefinedBehaviorSanitizer passed on the new contract; Microsoft MSVC/Windows CI and real hardware must be checked separately.

## Approved GUI plan (not yet enabled)

**Preserve the existing user-approved 10-strip + MASTER console**, faders, meters, FX inserts and current compact visual language.

- Main strip displays compact `SOURCE` (e.g. `IN 3`, `IN 5/7`, `CH 1`) and clearly distinct `MONO/ST` format indicator, not a misleading `M` icon confused with MUTE.
- Click SOURCE to open a lightweight anchored chooser of current valid hardware mono/stereo L/R pairs and internal sends. An inspector provides L and R independent selectors, source width/processing format, output bus, send pre/post fader, and input status. Only route kinds actually supported by native engine are enabled.
- Advanced Patchbay matrix visualizes all channels and ports without ten giant popovers. No blur/glow filters, no heavyweight hover shaders, no per-channel animation timers or fake meters. Use existing shared visual scheduler and byte-identical CSS/build assets.
- UI changes are **request → pending → native ACK/reject**, never optimistic localStorage authority for audio. Failure keeps the prior DSP route. Missing hardware is named, not silently reassigned.
- Preview mode uses a clearly labelled simulated topology and cannot be mistaken for real native audio.

## Next gates, strictly ordered

1. Review this P1B stacked PR against #108; Windows CTest + native build must PASS before integrating. Never merge the stacked PR directly into main without its parent P1A.
2. Native `DevicePortRegistry`: enumerate actual negotiated device channel masks and map physical ports to callback pointers, then verify on ASIO 4+/8+ channel hardware and WASAPI variants. No hidden channel re-open.
3. Implement per-strip prepared DSP state and mono/stereo plugin bus negotiation, then controlled graph publication with device epoch, ACK, smooth mute/ramp, and deferred destruction. Reuse existing LiveGraphHandoff patterns; do not co-own one VST processor in two concurrent execution graphs.
4. Add true SOURCE/MONO/ST controls and advanced Patchbay only when live native engine can execute them. Complete native sends, output mapping and latency compensation, plus portable state persistence.
5. Prove no-VST baseline #15 and perform multi-channel physical FAT, hotplug recovery, callback deadlines, audio isolation, clicks and real VST3 vendor states. Hardware test is the release gate.

**Definition of P1B completion:** checked-in pure routing code + passing deterministic release-mode assertions, not a new user-ready EXE.
