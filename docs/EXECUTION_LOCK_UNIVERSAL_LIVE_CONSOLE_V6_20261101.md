> **SEQUENCING UPDATE:** [RELEASE TRAIN v7](RELEASE_TRAIN_UNIVERSAL_CONSOLE_V7_20261101.md) is the active staged delivery and 3-thread synchronization contract, superseding this v6 plan where its merge/test order conflicts. PRD/ARD v5 and v6 audio safety invariants remain binding. Do not request customer test of an incomplete First Sound candidate.

# ArSonMix — EXECUTION LOCK v6 / UNIVERSAL LIVE CONSOLE
**Established:** 2026-11-01. **Role:** binding implementation plan; not an attestation of shipping features.
**Spec authority:** [PRD v5](PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md) and [ARD v5](ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md). v6 governs **execution sequencing, branch ownership and stop rules**, not a new product scope. Contradictory older milestone/GUI plans are historical, not actionable.

## 0. Why this lock exists / reality check
At source audit `main@bb6dff6b42158ba2db4666e0def54bb89abda4f8` Windows native CI [41263965810](https://github.com/masarray/arsonmix/actions/runs/41263965810) and Chromium [41263965813](https://github.com/masarray/arsonmix/actions/runs/41263965813) both PASS; Windows artifact is a **fixed 10-input+4-AUX physical FAT candidate**, NOT the universal console or hardware-clean proof. Native live callback still uses `LiveDryRoutingLab` with a separately leased, occupied CH1 external/Orbit chain. Draft [#266](https://github.com/masarray/arsonmix/pull/266) prepares identity-preserving dynamic PCM + M4 VST leases, but **does not switch the production callback**. No production CH11/independent Group/FX Return proof; [#15](https://github.com/masarray/arsonmix/issues/15) acoustic 0FX crackle not closed.

**One product outcome, not many incremental milestones:** 2 distinct verified hardware inputs -> CH1/CH2 real independent VST3 -> Vocal GROUP -> user-selected MAIN, plus parallel reverb FX RETURN and independent monitor bus -> physical audible output, with stable real-time faders and native ACK. Scalable ChannelId-based CH11 is an additional mandatory proof, NOT an optional mock. Never call an isolated UI screenshot or dry-only test 'finished'.

## 1. Product and operational decision records
1. **Live digital mixer only**: karaoke, stage FOH, podcast, streaming, radio. DAW-class **Console workflow**, not arranger/timeline/audio editor/recording. No forced OBS, virtual cable, vendor, device, or speaker.
2. **One realtime JUCE callback and exactly one accepted PCM graph**. Canonical typed `ChannelId`, `SourceBinding`, `ProcessingFormat`, `InsertId`, `SendId`, `PrimaryOutId` and `DevicePortRef`; displayed strip index never becomes identity. Control-plane off-thread graph prep + immutable atomically swapped snapshot + safe retirement. No second JS/DSP graph, second plugin engine or parallel CH1 output path.
3. **Native authority**: one `requestId + baseGraphRevision + deviceEpoch` intent, one accepted `ACK` and complete read-only snapshot. GUI never fabricates LIVE/PCM/plugin-installed state or silently defaults to IN1/OUT1. Failed requests leave last accepted audio operating, unless actual physical endpoint disappears; that fails closed.
4. **Two execution planes**: fader, pan, mute, solo, existing send gain and safe bypass are bounded RT control targets (no graph rebuild, mandatory mute or device restart). Source/format/channel/insert/send/route/Show graph edits are prepared on control thread, with one operator gesture. Hardware device and clock changes retain explicit safety authorization; no auto-ARM on hotplug.
5. **Every channel equally real**: Input CH1..N, independent Group, FX Return, Monitor and Main receive the same compatible native FX-owner/parameter/meter framework; their roles determine allowed sources/outputs. No permanent CH01 privilege. Migration must preserve original vendor processor instance, native editor, bypass ramp, Orbit state, AUX/sends, Show and monitored output. Do not amputate the working M4 engine simply to make a toy dynamic path.
6. **Direct operator console**: compact persistent IN, M/ST, FX INSERT, SEND, OUT, PAN/MUTE/SOLO, native meter, fader in each strip, pinned MAIN, channel banks and +CHANNEL only when native capability is accepted. Advanced Patch is optional for bulk/crosspoint. The workflow is one action and native ACK, not STAGE→MUTE→APPLY→START.
7. **Monitors and outputs** are user-selectable verified physical/virtual destinations with explicit pin/clock domains; avoid feedback cycles and double-dry. Device latency and VST-reported latency are disclosed: 'zero-latency' is no *unnecessary* DSP latency, never a claim of 0 ms physical.
8. **Performance first**: zero heap/locks/IO/plugin discovery in audio callback; stable 600px/768px/1080p geometry, avoid CSS blur, expensive hover layers and per-meter DOM remount. Retain existing proven SVG fader assets and reusable JUCE code unless measured evidence justifies replacement.

## 2. STOP / replace wrong architecture (controlled migration, not destructive deletion)
- **STOP** cosmetic-first progress, new modal/device-setup wizards as the routine source router, duplicate control owners across `web/app.js`, `web/bridge.js`, `web/monitor.js` and `web/mixer-io.js`, hardwired index-as-identity, and fake CH11/Group/FX/monitor display.
- **STOP** any 'FX only on CH1' fallback. Remove special-case public GUI routing after the actual M4 instance/editor is admitted into the universal publisher. Do NOT remove the *working* legacy CH1 code until PCM, plugin chunks, Orbit, Show and device outputs compare equal and rollback is tested.
- **STOP** unconditional global output MUTE on ordinary live control and safe graph-only edits; distinguish high-risk device/clock topology, which can still require guarded disarm.
- **STOP** accumulating experimental #263 dry Group, #251 default-OFF WASAPI click-removal, #225 unapproved FactoryFX in the production build. Preserve them as separate evidence/experiments; don't bulk merge into #266. Do not close #15 with CI evidence.
- **REPLACE** old multiple UI "staged/draft/live" booleans with a single accepted graph snapshot projection. Deprecate redundant menus/status banners **only after** the direct compact console has feature parity.
- **DEFER, not delete** automatic PDC/online plugin tail and sophisticated VCA/MIDI/OSC until the vertical first-sound/Group/FX Return passes. No new 'engine rewrite' when existing reusable JUCE/graph components already satisfy the seam.

## 3. Shared native ↔ GUI contract — planned; no claim currently implemented
Native publishes immutable `ConsoleSnapshot` with:
- `device: {epoch, backend, inputEndpoints[], outputEndpoints[], callbackPins[], sampleRate, bufferFrames, activePinMask, outputArmed}`;
- `graph: {revision, channels[{id,role,name,color,processingFormat,source,primaryOut,inserts[],sends[],meterIds,controls}], destinations[], capabilities{dynamicChannels,groups,fxReturns,monitor,sendsOnFaders,onlineGraphEdits}}`;
- `telemetry: {rawCapturePresent,channelMeterRmsPeak,mainOutputPeak,callbackLoad,vstLatencySamples,healthFlags}` (throttled; no simulated PCM).
Native accepts `ConsoleIntent {requestId,baseRevision,deviceEpoch,op,targetChannelId,payload}` and returns `ConsoleAck {requestId,status:ACCEPTED|REJECTED|REQUIRES_DEVICE_CHANGE,newRevision,errorCode,message,acceptedDiff}`. `PENDING` is local until ACK. Native serializes actions across UI panels; stale revision rejects with last accepted state. A real subscription/replay after editor reconnect must not duplicate commands.
**These names are a proposed interface to implement against existing JUCE bridge; do not ship made-up READY flags or duplicate JS routing engines.** GUI can develop using contract fixtures in tests, but a fake fixture must never be marked LIVE.

## 4. Parallel workstreams, strict ownership and dependency direction
| Lane | GitHub home | Exclusive primary ownership | Merge deliverable |
| --- | --- | --- | --- |
| **ENGINE** | [PR #266](https://github.com/masarray/arsonmix/pull/266), [issue #192](https://github.com/masarray/arsonmix/issues/192) | `engine/core/**`, `src/LiveAudioEngine.h`, `src/LiveDryRoutingLab.h`, native `Main.cpp`, C++ native tests | Complete one real publisher/callback, CH1 vendor + Orbit lease, FX/Show/AUX/MONITOR parity, dynamic real channels/Group/Return and native ACK |
| **GUI** | `work/operator-console-v6`, new GUI owner issue | `web/app.js`, `web/bridge.js`, `web/mixer-io.js`, `web/mixer-layout.js/css`, UI CSS, `web/monitor.js`, JavaScript/Playwright tests and embedded offline parity | Actual single-window operator console driven by accepted native snapshot, direct IN/OUT/FX/SEND and adaptive banks |
| **INTEGRATION + FAT** | [issue #192](https://github.com/masarray/arsonmix/issues/192), [issue #15](https://github.com/masarray/arsonmix/issues/15) | New cross-layer contract/integration tests, release verification and hardware FAT reports; no uncoordinated edits to engine/GUI owner files | One full-flow joined build, golden PCM, exact-head Windows artifact and physical device evidence |

**No parallel conflict:** engine never edits `web/**`; GUI never edits `engine/core/**` or JUCE callback; integrator does not silently alter either. Shared wire schema is coordinated in issue #192 and reviewed by both. Before merging, synchronize with latest `main` and re-run only the impacted consolidated gate. No three separate copies of the graph or source inventory.

## 5. Product releases are vertical acceptance gates, not micro-PR counts
### V1 — Full-fidelity native consolidation (ENGINE is dependency owner)
Bring #266 up to latest main. Migrate full legacy M4 CH1 external VST editor/Orbit + CH2..10/AUX/MAIN/MONITOR/secondary/Show to ONE accepted ChannelId publisher; switch the **existing** JUCE callback to exactly that renderer, not two. Compare rendered PCM on fixed physical pins and serialized plugin state/golden sessions. Reject or rollback safely; preserve old path until prove equivalence. GUI may build direct controls against schema fixtures in parallel, but does not advertise native support before capability ACK.
**Gate:** real 10CH+4AUX native plugin/mix/monitor/sends, zero RT allocations, no duplicate FX render, actual revision/epoch ACK, Windows exact-head CTest.

### V2 — Native scalable channel roles + operator console
Implement real CH11/CH-N, Group Selected (exclusive primary OUT → group → main), FX Return (parallel send, PRE/POST), MON/CUE output, and variable role-based banks; UI exposes +CHANNEL/Group/Return **only on native capability**. Every role owns genuine plugin slots, meter, fader, accepted routing. A hidden channel remains audible.
**Gate:** 2 real input pins independently assigned across nonadjacent logical channels, 2 separate hosted VST3s, Group and FX Return audio PCM, monitor independent from MAIN, no double dry, no false LIVE after rejects.

### V3 — Continuous live operation, online safety + durable Show
Stable input/source/insert/format changes with bounded transition and no ordinary full-mixer mute; safe VST prepare/retirement/bypass, deterministic process order and PDC disclosure; all identity/Show/plugin chunks restore or atomically reject. Driver/hotplug explicitly guarded. No latency guarantee outside measured device+buffer+VST.
**Gate:** repeated add/remove/plugin reorder/session restore without audio leaks or stale editor/ID, telemetry and callback deadline budget tests.

### V4 — Release-quality hardware FAT
Run measured Windows ASIO/WASAPI on at least one genuine multi-input device and normal PC/iGPU. 0FX wired 30-minute clean soak, interface IN1→CH7 and IN2→CH2, independent FX, Group + wet Return + separate MONITOR, CH/Master mute hardware silence, device failure/restore, cross-platform only after own gate. #15 remains blocker until real capture-versus-output crackle evidence.
**Gate:** one exact source SHA with Windows CTest PASS + Chromium PASS + verified packaged EXE + human signed physical run; no status 'PRO' from browser preview alone.

## 6. CI economy / governance
- **One meaningful PR per lane**, cohesive integration commits, no CI on each button/word. Contract tests locally or as focused job first; use existing **one Windows native and one Chromium workflow** for promotion checkpoints. Do not add a new workflow per test.
- Read failed job log first, fix underlying defect, avoid blind re-run; never weaken assertions about RT heap, PCM, devices or provenance merely to get green. Tests must target *supported behavior*, not stale implementation text.
- Hardware FAT is separate evidence from CI. Review branch file ownership and exact native/GUI revision before every release. Protect `main` from experimental dry-only cutovers. Avoid leaving old false UX live behind a new pretty facade.

## 7. Final Definition of Done (operator-first)
On real Windows hardware, operator selects device ONCE, uses compact console only to select `IN1→CH7`, `IN2→CH2`; chooses M/ST per channel; adds separate real VST3 to CH7 and CH2 (also possible before routing); creates CH11, groups two vocal channels, adds wet FX Return send without removing dry path, maps Monitor to chosen legal output and Main to another legal output, mixes continuously, saves/recalls Show, and physically hears correct signal. **No extra START / engineering matrix / whole-mixer MUTE for normal mixing.** App remains lightweight and status honest; no audio dropouts in measured physical soak. Anything short is a candidate, never a finished universal console.

### Workstream handoffs (open these as separate development conversations)
- [ENGINE](workstreams/ENGINE_UNIVERSAL_CONSOLE_V6.md)
- [GUI](workstreams/GUI_OPERATOR_CONSOLE_V6.md)
- [INTEGRATION + FAT](workstreams/INTEGRATION_FAT_CONSOLE_V6.md)
