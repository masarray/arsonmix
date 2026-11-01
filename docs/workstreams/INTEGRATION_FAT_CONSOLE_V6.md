# INTEGRATION / RELEASE / HARDWARE FAT WORKSTREAM — ArSonMix v6
**Coordination epic:** [issue #192](https://github.com/masarray/arsonmix/issues/192). **Hardware root issue:** [#15](https://github.com/masarray/arsonmix/issues/15). **Native PR:** [#266](https://github.com/masarray/arsonmix/pull/266). **UI branch:** `work/operator-console-v6`. Read [execution lock](../EXECUTION_LOCK_UNIVERSAL_LIVE_CONSOLE_V6_20261101.md), PRD/ARD v5. This lane coordinates, tests and releases; it does not invent a separate graph or own engine/GUI implementation files.

## Copy into a dedicated INTEGRATION/FAT development thread
Reconcile current `main`, engine #266 and GUI branch WITHOUT blind merges. Establish shared native `ConsoleSnapshot/Intent/Ack` wire contract; exact graph-capability gating and end-to-end tests. Own cross-layer verification/provenance and consolidate release only when the ENGINE native publisher/callback and GUI operator journey are both real. Keep existing one Windows native CTest workflow + one Chromium UI workflow; avoid fanout. Investigate issue #15 with physical ASIO/WASAPI, OUTPUT_ONLY_TONE, CAPTURE_ONLY and 0FX wired samples; do not 'fix' acoustic crackle by unverified filtering. Ship an EXE for practical hardware FAT only if its exact SHA builds and the operator FIRST SOUND path is supported. No pretend stable product.

## Merge and QA sequence
1. Identify exact branch SHAs and baseline full fidelity. Review ENGINE PR #266's CH1 external VST editor/Orbit, CH2..10, AUX/Show/MONITOR, callback and hardware pin/clock gate against pre-migration golden PCM. Block merging if it rejects standard production FX/Show or introduces dry-only mode.
2. Agree a versioned native `requestId + baseRevision + deviceEpoch` command and `accepted ChannelId graph + capabilities` snapshot; one writer, idempotent command ACK and one visible state. GUI should not need to parse private engine implementation or represent absent features as LIVE.
3. Merge engine feature only when exact-head Windows/CTest parity and rollback pass. Then GUI onto current main, run existing Chromium/JS and native integration tests on *same tree*. Hardware safety must not regress between branches.
4. Golden integration: capture hardware IN1/IN2 (or distinct validated sparse physical pins) independently to CH7/CH2, independent 1FX, Group Selected with exclusive serial OUT, parallel wet FX Return sends, MONITOR independent output, real MUTE/faders and session Show roundtrip. Test wrong pin, output alias, unavailable sample rate, stale generation and plugin crash; no surprise audible output after hardware change.
5. Package verified Windows x64 GUI with build SHA and EXE SHA-256, check WebView2 embedded GUI, native host binary and FIRST SOUND guide. `ZIP ready` means CI artifact; `Hardware certified` requires actual physical test.
6. Hardware run: 0FX physical mic→headphone 30 min at safe volume on ASIO and supported Windows Audio, exact backend/rate/buffer/pin list, raw PCM, post-FX PCM, MAIN and secondary output logs. Separate OUTPUT_ONLY_TONE, CAPTURE_ONLY for crackle classification. Mark glitches and copy full reports; don't infer DAC underrun from callback gap alone.
7. On typical Windows 11 iGPU PC, verify mouse/scroll smooth and audio deadline under multiple VST3. Physical low-latency metrics include device buffer and VST reported latency; no 0ms advertising.

## QA gating table
| Feature | CTest fixture | GUI/Playwright | Physical hardware |
| --- | --- | --- | --- |
| Direct independent pins | sparse callback / epoch validation | CH7 IN1, CH2 IN2 mapping readback | each input makes only chosen channel meter move |
| VST3 real per channel | distinct plugin identity + processor samples, silent lease | each FX editor/power/reorder native ACK | genuine audible change and bypass |
| Dynamic CH11 and Group | stable ID, exclusive edge, no cycles/double dry | +CHANNEL/Group only on ACK | Group fader actually changes combined sound |
| FX Return + PRE/POST | separate wet return PCM/phase/gain | SEND one-click / state restored | audible wet-only return in parallel with dry |
| MONITOR + output | verified separate pin/clock/alias guard | direct OUT and Send-on-Faders | outputs and mute behave independently |
| Show and resilience | plugin chunk + graph/ID/pin mapping roundtrip | reject states preserve UI and audio | hotplug recovery with explicit re-ARM |
| Perf / crackle | RT zero heap/lock + deadline | standard iGPU low-compositor-load smoke | physical 0FX 30 min + loaded FX soak |

## Coordination and CI discipline
No owner edits under `engine/core/**` or `web/**` without first coordinating in #192. Create isolated integration tests rather than patching engine to satisfy a UI string assertion. CI stage only after meaningful combined checkpoints, no repetitive release upload before passing tests. Record one status row per exact SHA: Native | UI | Artifact | Physical FAT. Failing column prevents production label. Unattended thread/agent work is NOT implied by issues; separate agents must explicitly be started with these handoff docs.
