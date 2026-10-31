# M5 — Universal ChannelId / native VST3 lease migration

**Scope:** ArSonMix is a realtime live-performance digital mixer, NOT a DAW arranger/editor. This milestone migrates the actual M4 per-channel vendor processor ownership model into a ChannelId-keyed native DSP graph. It is the required shared FX seam for real CH11+, Group and FX Return parity. It does **not** activate the old dry-only M3E #263 pilot in production.

## Why

Current shipped M4 supports native VST3 on fixed CH1..CH10 and four AUX lanes, with stateful FX instance identity, bypass fade and correct control-thread retirement. Prior M3E pilot could create a new Group native PCM but refused any existing VST3/AUX/MONITOR/Show. Replacing an accepted M4 mix with an FX-free graph would silently discard reverb and compressor state. This is unacceptable for live karaoke, podcast, streaming or stage use.

Top global console contracts are consistent:
- Fender Studio Pro: Bus submixes, FX Channel return with independent sends, pre/post send choices, Group channel input fan-in, nested Bus and direct output routing. https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm
- Waves eMotion LV1: Channel and Group processing, separate Aux/FX/Monitor/MATRIX layers and physical routing patches. https://www.waves.com/emotion-lv1-tutorial-4-2-mixer-channels-groups-aux

## Implemented M5 native architecture

### `engine/core/DynamicChannelFxLeaseBank.h`
- A **stable ChannelId → M4 FX rack** map. Identities survive reordering and channel-bank viewport changes. Works for arbitrary dynamically admitted Mono/Stereo input, Group, and FX Return IDs; no CH10 borrowing.
- `captureLegacy`: imports each already-accepted M4 native vendor **FxInstance lease** using exact opaque `instanceId`, never a catalog-name substitute or processor recreation. The actual `juce::AudioProcessor`, parameters, binary state, internal delay/reverb tail, bypass envelope and MIDI scratch remain owned.
- `prepare`: one control-thread transaction preallocates and validates mapping, 8-insert bound, mono/stereo Node contract, stereo VST bus layout, missing lease, reused native processor in multiple simultaneous lanes, invalid device epoch or repeated ChannelId. Failed preparation leaves the previously accepted bank unchanged.
- `operator()(ChannelId,float*,float*,frames) noexcept`: callback dispatch is read-only, binary search by immutable ID with raw processor pointers, no `shared_ptr` copy, allocation, waiting, mutex, VST scan, new callback or dynamic catalog lookup.
- **Lifecycle requirement:** lease bank MUST be owned/retired by the SAME native prepared PCM publication/snapshot as its routing plan. A final owner must never disappear inside a callback. Two graphs sharing the same plugin may NOT render concurrently; existing `FxInstance::render` fail-closed atomic gate is an additional safety fence, not a dual-render design.

### `src/LiveDryRoutingLab.h`
- `captureDynamicFxLeases(expectedEpoch,expectedRevision,ids,...)` now exposes exactly 10 input + 4 AUX M4 VST rack owners for native M5 conversion, from the actual publisher accepted revision. No device switching and no modification to the active M4 graph. Stale/invalid device epochs fail closed.

### `tests/live_channel_fx_graph_test.cpp`
- Native JUCE `AudioProcessor::processBlock` through ONE `DynamicNativeSessionPublisher` PCM graph: **CH1 FX + CH11 FX → GROUP FX → MAIN** while CH11 also has a parallel POST SEND to an independent FX Return with its own processor.
- Proves an actual output sum with no doubled dry signal and correct -6 dB Group fader, opaque M4 instance token reuse after the old owner graph is retired, no audio-callback heap allocations, one native render, duplicate vendor instance rejection, and wrong-epoch output fail-silent.
- Reuses existing `ARMixLiveChannelFxTests` Windows MSVC/CTest executable; no separate CI matrix and no expensive external vendor plugins for deterministic tests.

## Integration admission — do not skip

1. Merge this bank only after exact-head native Windows CTest and test fixture pass.
2. Refactor M3E dynamic publisher to **own Bank alongside native prepared PCM**, so an epoch/revision ACK refers to the same routing + FX instance generation. No separate second render path. Use the M4 off-RT plugin editor/Show save-restore; CH1 legacy external owner must be migrated independently (the legacy routed graph can have an empty CH1 FX lane while the special CH1 host still has FX). Never claim a complete CH1 transfer until that owner is connected.
3. Migrate the entire accepted legacy Show/AUX/PRE/POST/MONITOR/MAIN/secondary output via the existing `LegacyGraphMigration` bridge. Support one hardware device pin mask per accepted render; no hidden coercion to ASIO/OBS.
4. Permit Add Channel/Group/FX Return in the single native producer **after** passing true CH11 mono/stereo, plugin preset state, nested Group and send/monitor tests. Build GUI strip IDs from the exact native ACK. Hardware FAT follows from a Windows executable at that head.
5. Keep existing M4/FAST3 one-APPLY and emergency MUTE, adaptive channel bank R7, P0 #15 0FX crackle evidence and realtime performance counters intact.

**Honest status:** This is a substantive native FX/state migration seam with real processor PCM tests, **not yet a publicly usable full dynamic Studio One-equivalent Console**. No physical zero-latency guarantee and no field MAONO PS22 FAT has been executed for this M5 branch.
