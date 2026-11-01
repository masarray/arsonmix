# ENGINE WORKSTREAM — ArSonMix Universal Live Console v6
**Branch/PR owner:** existing [draft PR #266](https://github.com/masarray/arsonmix/pull/266) only, plus epic [#192](https://github.com/masarray/arsonmix/issues/192). Do **NOT** start another native dynamic-engine implementation. Canonical scope is [PRD v5](../PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md) + [ARD v5](../ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md) + [execution lock](../EXECUTION_LOCK_UNIVERSAL_LIVE_CONSOLE_V6_20261101.md).

## Copy into a dedicated ENGINE development thread
Continue `masarray/arsonmix` **directly via GitHub**, from #266 current head. First reconcile #266's old base against today's `main` and its post-2026-11-01 FX silent-prep / CH1 fixes; preserve only compatible changes. Do not accidentally wipe main. The goal is NOT dry-only or a second callback: make the actual production JUCE callback render **one** ChannelId-typed prepared graph with real M4 VST3 instances, including occupied external CH1/Orbit, CH2–CH10, four AUX, existing PRE/POST sends, MAIN/MONITOR, secondary outputs and Show. M-U1 must be full-fidelity. Only after that, extend to CH11/Group/FX Return independent native audio nodes. Use real audio PCM tests and keep a last-good rollback path. No hardware claim from unit tests, no fake +CHANNEL, no mandatory OBS or capture endpoint. Only meaningful integrated milestones, optimized code, maintainable and real-time safe.

## Ownership / code seams
- Own `engine/core/**`, `src/LiveAudioEngine.h`, `src/LiveDryRoutingLab.h`, native `src/Main.cpp`, C++ native tests. UI branch exclusively owns `web/**`.
- Existing #266 pieces: `UnifiedLegacyProductionAdmission`, `LegacyLiveGraphMigration`, `DynamicChannelFxLeaseBank`, `DynamicNativeSessionPublisher`. Source-grounded blocker: they are **off-callback preparation**. Wire only ONE renderer in `audioDeviceIOCallbackWithContext`; don't mix legacy output in parallel or discard vendor state.
- Verify `processLegacyChannelOne` -> universal lease migration preserving parameter/editor/pending bypass/Orbit identity. When original CH1 vendor cannot be migrated, return explicit failure and keep current accepted renderer; no success badge.
- DevicePortRef/ChannelId stable; never derive a second pin by left+1 or confuse display index with source callback index. Preserve output and clock-domain guards. Graph cycle detection and CPU/scratch bounded admission.
- Source/Format independent: mono mic + stereo processing with deliberate conversion, two independent true stereo meters. Group's exclusive serial primary destination vs reverb's parallel PRE/POST send must have golden PCM oracles.
- Controls: fader/pan/mute/solo/send gain via bounded no-heap RT parameter plane; structural changes through serialized `requestId,deviceEpoch,baseRevision` graph publish and typed ACK. Not every topology change needs user MUTE; hardware clock/device swap is exception. Plugins prepared off RT with no host scan, mutex, allocation or refcount in callback.
- SHOW preserves stable graph IDs, plugin state chunks and logical I/O mapping; refuses invalid endpoint/alias/hotplug safely.

## Engine acceptance harness before any merge
1. Exact output L/R PCM parity old M4 vs new ChannelId for occupied CH1 VST editor+Orbit, CH2 and CH10 real processing, 4 AUX returns, PRE/POST, MONITOR & MAIN and declared secondary output; validate no doubled dry/monitor alias.
2. Stale device epoch/revision, incompatible VST format, plugin throw, feedback cycle, CPU/scratch limit, rejected Show and absent capture pins preserve previous graph (except true lost device fails mute).
3. CH11 native mono/stereo inserts identical to CH01, Group Selected and FX Return native sends affect physical PCM; plugin instance owned once and survives reorder/route changes. 0 allocations or locks during render, stress buffer sizes.
4. Exact-head Windows MSVC + existing CTest workflow green. Do not mark PR #266 ready or merge until full-fidelity callback switch and compatibility gate. Keep [issue #15](https://github.com/masarray/arsonmix/issues/15) hardware FAT open.

## Coordination protocol
Before schema edits, post proposed snapshot/intent/ACK mapping in issue #192 for GUI author; prefer existing bridge fields whenever semantically correct. Document any departure from v6 contract. Never write UI markup here. At each meaningful PR checkpoint post exact head SHA, native tests and sound-path truth; never say CH11 live from an offline graph fixture.
