# AR MIX Studio — Architecture Requirements & Design (ARD)

> **HISTORICAL ARD ONLY (2026-10-25):** The current user-approved mixer UX/physical I/O/AUX planning authority is [PRD v2.0](PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md) and [ARD v2.0](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md). This old proposed CH1-only/12ch mock/old roadmap is not current source truth. Keep its original reasoning as provenance; preserve Smart Audio realtime safety contract and native code tests. Do not rebuild v0.3 UI or hide FX Rack under routing.


**Version:** 0.3.0 · **Date:** 25 October 2026 · **Status:** HISTORICAL 0.3 DESIGN; superseded for native-device architecture by Smart Audio Engine ARD v1.0 (NOT production readiness certification)  
**Product:** Free and open-source Windows-first low-latency VST3 host for live karaoke, singers and livestream creators.  
**Primary principle:** **FX Rack First, One-Click Live, Realtime First.**  
**Implementation status (updated 2026-10-25):** GUI v0.8 and experimental P2.5.1 JUCE Windows/VST3 Channel 1 native engine **are implemented in main**, but crackling was observed with zero plugins on actual hardware (P0 issue #15). This v0.3 document preserves original design history and must **not** be used as the current implementation status, current GUI specification or as authority to proceed to P2.6. See the canonical v1.0 architecture below.

---

## Current architecture (overrides outdated v0.3 proposals)

1. **[Smart Audio Engine ARD v1.0](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md)** — authoritative requirements, realtime safety, clock-domain, backend selection, and reliability gates.
2. **[Smart Audio Engine Implementation Blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md)** — precise API modules, algorithms, PR sequence, test harness, hardware FAT and handoff checklist.
3. **[P0 issue #15](https://github.com/masarray/arsonmix/issues/15)** — unqualified zero-FX crackling; prioritize objective I/O tests before features.
4. **[Locked GUI v0.8](GUI_BASELINE_LOCK_2026-10-25.md)** — sole approved visual source. Existing Easy/Pro and page-layout text below is historical where it differs from v0.8.

The historical section following this notice is retained for traceability. An older aspirational requirement is not evidence that the product already implements it.

---

## 1. Executive decision

Build an independent VST3 host with a polished digital-console experience. It must not become a DAW clone, a reskinned web application, or a visual demo marketed as an audio mixer. An amateur singer should be able to choose a microphone and output, recall a vocal chain, check the level, and start performing without configuring a professional patchbay.

**Recommended stack:** C++20, JUCE (AGPLv3 pathway), WebView2/HTML/CSS/SVG (embedded offline, no remote server), JUCE VST3 hosting, JUCE Windows audio integration with ASIO and WASAPI; add a dedicated low-period WASAPI adapter only if profiling justifies it. Steinberg VST3 SDK is MIT-licensed; the ASIO SDK offers a GPLv3 path, but verify SDK license notices, version and distribution obligations before release. **Do not confuse an audio driver API with virtual audio cable functionality.**

**Live optimization principle:** predictable work, bounded memory, fixed-capacity event paths, separation of real-time and control work, staged graph updates, and measured latency/dropout quality. “More threads,” lock-free everywhere, or splitting each plugin into a thread are *not* automatically faster. Complexity must improve a measured hotspot.

## 2. Product definition and exclusions

| Dimension | Requirement |
|---|---|
| Audience | Singer, streamer, live performer, karaoke enthusiast, beginner |
| Primary action | Add or recall an ordered FX chain, not edit an entire DAW timeline |
| Visual identity | Dark graphite / machined metal, crystal-candy controls, colored channel identity, clear status |
| Main surfaces | MIX / FX RACK / SIGNAL FLOW / SETUP; Easy and Pro modes |
| Core value | Reliable vocal enhancement, clear signal path, low monitoring latency |
| Storage | FX Chain Preset (per-channel plugin state), Mixer Snapshot (full mixer), Session |
| Devices | Windows ASIO / WASAPI (shared and exclusive according to availability) |
| Host target | VST3 audio effects; VST instruments / MIDI / external clock are later milestones |
| Out of scope for first demo | DAW editing, multitrack timeline, AES67, ARA, cloud sync, mobile clients, AI auto-mastering, universal plugin crash sandbox |

### 2.1 Three critical journeys

**First-time setup:** Open → select audio interface and driver → select mic input + monitoring output → display loopback/feedback warning → choose preset (e.g. Warm Karaoke) → live-ready check → enable monitoring. Every action should have inline sensible defaults and a clear explanation, not a large modal matrix.

**Signature voice:** Choose vocal channel → **+ ADD VST3** → search & choose effect → drag to reorder (or Move Up/Down) → open authentic plugin editor → bypass/test → Save FX Chain preset → Load chain before another live session. The host tracks the actual vendor plugin state, not a screenshot of knobs.

**Go live:** Select Mixer Snapshot → resolve plugins and devices → arm output safely → show level/clip/Xruns/latency/device clock → monitor sound → one-click safe mute/recover when an output disappears. Snapshots must not accidentally change the output device while users perform.

## 3. UX information architecture

```text
AR MIX STUDIO
├── MIX                       permanent faders, LED meters, mute/solo, channel names
├── FX RACK [PRIMARY]         ordered VST3 chain, + Add, bypass, reorder, plugin UI
├── SIGNAL FLOW               Input → chain → mixer → Master → monitor / stream
├── SETUP                     driver API, physical devices, buffer, rate, signal checks
├── QUICK SOUND PRESETS       factory vocal chains (one action)
└── SNAPSHOTS / SESSIONS      save/load, recent, recovery, export/import
```

**Easy mode (default):** Full-width visible faders, clear FX slots, PAN, levels, MUTE/SOLO, Master, no deep engineering items until requested. **Pro mode:** Trim, HPF, sends, routing matrix, performance counters, plugin latency and advanced audio configuration. Easy/Pro share the same model and commands; they must not create two versions of DSP behavior.

### 3.1 Component design rules

- Selected cyan control has *continuous* gloss from top to bottom and no harsh horizontal center line. Pressed, disabled, focused and hover states must be distinguishable even without color.
- Metallic faders have a bounded travel track: thumb never extends beyond the safe top/bottom margin, including high-DPI Windows scaling.
- Track name is fixed at the **bottom**; plugin rack is at the **top**; pan/mute/solo occupy the middle; fader dominates lower half.
- The rack is visible and editable from mixer overview; deeper rack view supports reordering, bypass, offline plugin listing and actual vendor editors later.
- Under load, only active values/meters change; avoid rerendering all channel DOM elements for each fader movement.
- At 1366×768, allow horizontal channel scrolling; never shrink text/controls to illegible sizes. Master always remains discoverable.
- Never simulate an actual dB meter, CPU load, driver connection, or latency status once the app claims to be connected. Demo mode must be clearly labeled.

## 4. Architectural decomposition

```text
                       ┌────────────────────────────────┐
                       │ UI  WebView2 (HTML CSS SVG)     │
                       │ mixer/rack/flow/presets         │
                       └──────────────┬─────────────────┘
                                      │ typed commands / versioned telemetry
                       ┌──────────────▼─────────────────┐
                       │ CONTROL PLANE (non-RT)         │
                       │ validate / scheduler / state   │
                       │ UI bridge / async load/save    │
                       └─────┬──────────┬───────────────┘
                             │          │ worker tasks
              atomic params  │          ▼
             SPSC events     │    ┌──────────────────────┐
                             │    │ Plugin scanner worker│
                             │    │ serializer / prep     │
                             │    └──────────┬───────────┘
                      ┌──────▼───────────────▼───────┐
  ASIO / WASAPI  ────▶ │  AUDIO ENGINE (RT callback)   │ ────▶ device OUT
                      │  bounded graph / reusable DSP │
                      │  VST3 processBlock / meters   │
                      └──────────────────────────────┘
```

**UI must never run on the audio callback.** Embedded WebView2 is not the processing engine. Commands and telemetry are messages: the UI is replaceable without rewriting DSP.

### 4.1 Modules and responsibilities

| Module | Ownership | Runtime / safety |
|---|---|---|
| `host-app` | window, resource provider, lifecycle, shutdown | Message thread |
| `control-bridge` | schema check, clamp, ACL, dedupe/coalesce, rate limits | UI/control threads |
| `device-service` | enumerate, validate, prepare ASIO/WASAPI stream and clock domains | Control worker; callback when running |
| `plugin-catalog` | search, hash, IDs, compatibility, scan failure cache | **Separate scanner process** preferred |
| `graph-builder` | validate channels, plugin buses, topology, memory budgets, prepare | Non-real-time worker |
| `audio-engine` | run prepared graph, mix, meter, write to driver | **Real-time callback** |
| `state-store` | sessions, plugin state, snapshots, journals, migrations | Storage worker |
| `telemetry` | xrun count, CPU time, peak/RMS, plugin latency, safe status | Aggregate off callback |
| `core-dsp` | reusable gate, EQ, dynamics, gain/pan, meter, limiter | Prepared deterministic DSP |

## 5. Real-time audio safety contract (mandatory)

1. After `prepareToPlay` and before audio starts, allocate/reuse all normal audio working buffers. `processBlock` may never request disk I/O, network, UI, JSON parsing, VST scanning, file dialogs, graph construction or session serialization.
2. No unbounded mutex wait, blocking condition variable, timer waiting or thread join on the audio callback. Avoid `new/delete`, unbounded containers, logging, `std::function` allocation, or `shared_ptr` destruction in the callback. In particular, *third-party VST3 plugins are not guaranteed real-time-safe*: profiling and quarantine are necessary.
3. Process the precompiled graph in deterministic topological order. Use flat arrays of nodes and preallocated interleaved/noninterleaved scratch views rather than recursive linked structures.
4. Coalesce continuous fader/pan/trim parameters **by canonical parameter ID**, not by human-readable label. Audio thread reads the latest atomic value once per block and performs a short gain ramp within the block to avoid zipper noise.
5. Discrete events (mute, solo, transport) must retain ordering. Use a **bounded SPSC queue** between a *single control dispatcher producer* and the audio callback. If the queue is full, report/backpressure and retry without losing a critical change. Do not silently drop safety mute operations.
6. Build and prepare modified graphs off-thread. At an audio-block boundary atomically switch to a prepared immutable plan. Retirement/destruction is deferred until the old plan is no longer referenced by the callback. Use epoch/acknowledgment and backpressure: never free an in-use graph on the audio thread.
7. If a plugin's latency/tail/bus topology changes, update the **next staged graph**, not the current graph in-place. Fade or mute intelligently during unsafe transitions.
8. UI commands are validated and bounded (channel IDs, finite numbers, enumerations, plugin identity, buffer limits). No code execution from session JSON.
9. Avoid CPU frequency/power “optimizations” without measurements. Record power plan, sample rate, buffer, PC CPU, interface model and driver version in every benchmark.

### 5.1 Why this matters

At 48,000 Hz with a 64-sample buffer, each audio callback period is **1.333 ms**. At 128 samples it is **2.667 ms**; at 256 samples **5.333 ms**. This is **buffer duration, not measured round-trip latency**. Capture/output driver buffering, resampling, plugin group delay, converters, and the operating system add latency. Microsoft describes both ASIO and Windows low-period WASAPI and the limitations of exclusive-mode ownership.

**Illustrative target** (must be measured on actual hardware): 48 kHz, 128 samples; p99 DSP callback work under 40% of buffer deadline (~1.07 ms), max non-RT UI impact negligible, 0 unhandled dropouts in a 60-minute release test, and a measured low-latency performer preset whose plugin reported latency is near zero. These are release goals, not current product capabilities.

### 5.2 Threading model

| Thread/process | What it does | Forbidden |
|---|---|---|
| Audio callback | graph processing, bounded param read, meter accumulator | Locks, file I/O, scan, allocation, logging |
| UI message thread | renderer bridge, button/fader command validation | Lengthy scan/load/serialize, blocking join |
| Control dispatch | merges continuous controls, handles ordered transitions | Real-time callback work |
| Scanner *process* | loads unknown VST3 binary, extracts metadata, times out | Write directly to active graph |
| Graph preparation worker | instantiates/initializes plugins; validates graph | Free live plan before RT ack |
| Storage worker | saves snapshots/binary plugin state with checksums | Triggered from audio callback |
| Telemetry worker | aggregates 10–30 Hz meter state, latency status, counters | Per-sample UI messaging |

**Caution:** Loading third-party VST in a separate scanner process protects discovery. It does **not** isolate crashes of an in-process plugin during actual audio processing. Optional plugin-DSP process isolation is a later feature with explicit IPC, memory, synchronization and added latency tradeoffs.

### 5.3 Canonical command/event protocol

```json
{
  "schemaVersion": 1,
  "sequence": 9012,
  "kind": "parameter.set",
  "target": {"channelId": "ch-001", "pluginInstanceId": null, "parameterId": "fader.gainDb"},
  "value": -5.2,
  "source": "ui.drag"
}
```

Commands: `parameter.set`, `mute.set`, `solo.set`, `plugin.insert`, `plugin.remove`, `plugin.move`, `plugin.bypass`, `preset.apply`, `snapshot.recall`, `device.prepare`, `device.activate`. Ack reports `{sequence,status,revision,errorCode}` from the control plane. Structural requests are processed asynchronously. Only validated scalar updates go to the atomic parameter bank.

### 5.4 UI telemetry

Aggregate the audio peak/RMS into a fixed-size meter snapshot with monotonically increasing sequence; transfer to WebView no more than 20–30 Hz in background or 30–60 Hz while foreground, depending on profile. Throttle/stop when hidden. Never send a JavaScript event per audio sample or audio callback. Render CSS transforms or lightweight SVG values without expensive full-tree rerenders. Track and display xrun/dropout counts from the backend rather than estimating them from CSS meter animation.

## 6. Audio graph and low-latency processing

```text
Mic input (driver)
  → input trim / DC filter / input meter
  → ordered VST3 inserts (user editable)
  → smooth gain & pan
  → channel/bus summing
  → master limiter (optional) & master meter
  ├──→ physical monitor outputs
  └──→ streaming output route (requires supported driver/virtual endpoints)
```

For P1 build one mono-input vocal channel to stereo output, then add a separate music input, master fader and mute. Explicitly handle sample format conversion, stereo/multi-bus layouts, silence flags, channel maps, plugin tail, and bypass ramping. Master mute must remain operable if the WebView hangs (native safety fallback).

**Graph discipline:** immutable plan representation with per-node input/output bus indices, scratch buffer offsets, plugin channel layout, latency samples and routing. Validation rejects cycles except an explicitly designed feedback/delay node. A plugin might report a different latency after sample-rate/buffer changes: listen for notifications and stage the update.

**Latency compensation:** accumulate plugin `getLatencySamples()` per path; add delay compensation on parallel routes where needed. Do not imply this lowers monitoring latency: compensation can only delay the faster route. Offer **LIVE low-latency profile** that rejects/warns about high-latency look-ahead or linear-phase plugins, plus **STUDIO profile** for quality over monitoring latency.

## 7. VST3 host requirements

**P1 acceptance:** discover installed `.vst3` bundles; identify classes by stable plugin IDs, vendor and version; instantiate from native factory; verify input/output buses and sample format; display generic editor initially, and native plugin editor when available; process a real signal; support bypass, reorder, replace, parameter changes and safe unload; persist/restores full plugin processor/controller state.

**Plugin scanner:** worker process separate from the audio host, timeout per plugin, hash path + binary version, cached results, blacklist/quarantine crashers, count failures, manual rescan. Scanning does not happen during playback activation. Native plugin editors must be hosted with correct parenting/thread affinity, without making WebView hold plugin raw pointers.

**Plugin lifecycle state machine:** `Discovered → Validated → Instantiated → Prepared → Active → Bypassed/Inactive → Retiring → Released`; `Quarantined` for scanner failures. Moving a plugin in the chain creates a new prepared order and commits only after safely resolving processing state.

**Potential plugin bugs:** some effects allocate, block, crash, take multiple seconds to instantiate, or open untrusted editor windows. Provide “known safe for live” hints based on measured evidence, never guarantee third-party effects are realtime-safe. If a plugin is unavailable, show placeholder, maintain its stored state blob and let the user locate a compatible replacement rather than silently dropping it.

## 8. FX presets, mixer snapshots and sessions

**Three storage scopes must remain separate:**

- **FX Chain Preset:** ordered list of VST3 instances, unique plugin IDs, version fingerprints, bypass flags, complete plugin DSP + editor state, wet/dry and mapping. Portable between compatible sessions.
- **Mixer Snapshot:** all channel faders, pan, mute/solo, send states, routing, FX chains, plugin settings, master bus states, per-snapshot device policy. Sensitive output/device selection is locked by default for live recall.
- **Session:** snapshot library, channel definitions, device profile, recent session state, UI preferences, plugin diagnostics, version metadata and autosave recovery log.

Use versioned `.armix` session container (ZIP with a safe, deterministic manifest, SHA-256 hashes, separate binary plugin-state blobs). Suggested manifest fields: `schemaVersion`, `sessionId`, `createdAt`, `channels[]`, `routes[]`, `pluginInstances[] {instanceId,classId,vendor,version,format,latencySamples,stateComponentPath,stateControllerPath}`, `deviceProfile`, `snapshotPolicies`, `integrity`. Validate file sizes, path traversal, channel counts, malformed JSON and plugin state size before load.

Steinberg VST3 persistence calls include component and controller state; to restore component state first, then synchronize the controller state. **Never treat saving only normalized on-screen knob values as an adequate substitute for plugin binary state.**

### 8.1 Safe snapshot recall

1. Select snapshot and preview changes, including plugin needs and device changes.
2. Validate all plugin IDs and availability off-thread; stage plugin instances and their complete state.
3. Prepare routing/buffers off-thread, compute latency and detect dangerous feedback/route loops.
4. On live confirmation, atomically replace prepared graph at a block boundary with bounded mute/crossfade where feasible.
5. Publish results with warnings for missing plugins; preserve older graph until no longer referenced. Device switch is a distinct operation, not a surprise side effect of a preset.
6. Persist the rollback state; if recall fails, restore previous stable mixer without emitting sudden full-level audio.

A **single global mutex around the graph** is explicitly prohibited as the snapshot implementation.

### 8.2 Current v0.3 UI prototype limitations

The v0.3 trial stores channel-level chain names, faders, pan, mute/solo, mixer snapshots and user chain presets in browser storage, and offers JSON export/import. It **does not save real VST binary state**, does not scan installed plugins and does not process any microphone or music audio. These local UI-only states are for validating user journeys and will be migrated to the native state-store in P1/P2.

## 9. Beginner-friendly routing and safe defaults

Use visual route blocks: **Mic → FX Chain → Mixer → Monitor / Stream**. Allow manual advanced patching only in Pro. On default setup, choose a single audio device where capture/output share clock; do not silently aggregate multiple ASIO devices. Separate music/capture and monitoring only after explaining clock/drift and buffer implications.

**Feedback safety:** default microphone monitoring off until user chooses monitor output, smooth gain ramp from silence when enabling, clear clip warning, “panic mute” always visible. Prevent common loop routes (capture stream output routed back into mic processing) and explain exactly which external streaming capture output is being sent. Dedicated virtual audio output usually requires a separate virtual device/driver unless one is shipped and maintained deliberately.

## 10. Visual performance architecture

Frontend packages all assets locally via JUCE `BinaryData` and `WebBrowserComponent::withResourceProvider`. No CDN dependency. Coalesce incoming parameter writes from pointer drags before crossing the JUCE/native bridge; preserve final exact release value. Update only the affected display/fader, not the entire mixer. Cache meter elements, batch style updates using `requestAnimationFrame`, and throttle background updates. Do not use drop shadows, backdrop filters, expensive SVG path recalculation or huge PNGs inside hot meter loops.

**Texture policy:** metal/candy visuals are CSS/SVG components with bounded overlays and gradients; no 12 separate heavyweight canvas scenes every frame. Live performance may use software rendering on some hardware, so test GPU and integrated GPU. Screenshot-based automated geometry regression protects against clipped caps, gloss seams, wrong z-order and text cropping at 100%, 125%, 150% Windows scaling.

## 11. Licensing and intellectual property

- **JUCE:** dual AGPLv3/commercial; use the AGPLv3-compatible route and publish corresponding source and build requirements.
- **Steinberg VST3 SDK:** MIT, preserve notices.
- **Steinberg ASIO SDK:** proprietary or GPLv3 path depending on SDK/file; verify exact current distribution and compatible obligations in an open-source build. JUCE warns that enabling `JUCE_ASIO` introduces ASIO SDK dependencies.
- **Third-party VST3 plugins:** host software does not automatically grant permission to redistribute plugins or presets/assets from them. Discover the user's installed plugins; do not bundle proprietary binaries by default.
- **Soundcraft/DiGiCo/Waves:** visual quality can inspire original design; do not copy proprietary trademarks, shipped graphical assets or code.
- **Recommended repository license:** **AGPL-3.0-only** for app + GUI if JUCE is used via AGPLv3; legal compatibility review before public release. Reusable, independently authored DSP may be under a separately chosen compatible license if split into genuinely independent modules.

## 12. Production readiness gates (test matrix)

| Gate | Scenario | Pass definition |
|---|---|---|
| RT-01 | 48 kHz / 64, 128, 256 samples | Supported sizes enumerated; stable stream; explicit buffer errors |
| RT-02 | 60-min stress with typical vocal chain | Zero **unhandled** dropouts for the chosen published supported-hardware configuration; report all xrun counters |
| RT-03 | drag 12 faders quickly while DSP runs | No audio thread locks/allocations from host paths; no audible zipper noise |
| RT-04 | plugin scan with corrupt or crashing plugin | Scanner process fails/quarantines item; host continues running |
| RT-05 | insert/remove plugin during playback | No use-after-free, plugin state misuse, stuck audio, dangerous full-level burst |
| RT-06 | plugin latency changes | Total latency display updated; clear warning for high-latency plugin |
| RT-07 | device disconnect/reconnect | Safe silence; explicit recovery flow; no uncontrolled route switch |
| ST-01 | save/load a VST3 chain | Plugin processor **and** controller states match after restoration |
| ST-02 | snapshot recovery with missing plugin | Placeholder, warning, prior state retained without data loss |
| UI-01 | 1920×1080, 2560×1440, 1366×768 | Controls fit / scroll intentionally; no clipped fader heads or gloss seams |
| UI-02 | 100/125/150% scaling | Metal cap, buttons and text have no clipping/overlap |
| UI-03 | heavy meters & animation | Foreground UI target 60fps on supported hardware; no audio callback timing regressions |
| QA-01 | sanitizers + concurrency | Unit tests, ASAN/UBSAN where applicable, Windows AddressSanitizer, ThreadSanitizer where supported, soak tests |

**Measurement toolkit:** audio loopback round-trip pulse (physical input/output) with independent reference, frame-time histogram, `std::chrono` callback work timing in a preallocated telemetry ring, callback p50/p95/p99/max, device callback count/xruns, process CPU/memory, plugin load spikes, memory footprint after 10/100/1000 chain changes, snapshot-recall success/fail counts. Run device-specific benchmarks; do not copy marketing latency figures.

## 13. Implementation roadmap — fastest credible demo

### P0 — COMPLETE IN THIS SESSION: UX architecture & interactive frontend

- [x] Re-scope product as live VST host with FX Rack priority.
- [x] Fix selected glossy button surfaces and fader travel geometry.
- [x] Add **FX Rack**, mock plugin browser, add/reorder/bypass/remove visual FX and generic parameter inspector.
- [x] Add Easy/Pro switch, Quick Sound presets, Signal Flow, Windows device setup concept.
- [x] Add UI-scoped mixer snapshot, FX preset, JSON export/import paths.
- [x] Add C++20 bounded SPSC queue/parameter coalescing primitives and native tests.
- [x] Run headless Chromium interaction smoke test; inspect screenshots.
- [ ] Connect physical audio, native plugin scan and real VST3 state: **not done**.

### P1 — FIRST SOUND (next highest-priority implementation session)

**Goal:** native Windows executable accepts an ASIO or WASAPI mic, hosts **one real VST3 plugin** and routes audio to selected output while retaining the UI.

1. Wire JUCE AudioDeviceManager / AudioProcessorPlayer with explicit WASAPI selection and conditional GPL-compatible ASIO enablement; test on Windows 11 PC.
2. Implement input + output device wizard, channel map, sample rate/buffer negotiation and sample format conversion, error reporting and a native panic-mute action.
3. Build single-channel prepared DSP path with gain smoothing, peak meter telemetry and real audio I/O; test direct microphone pass-through before introducing any plugin.
4. Build separate `plugin-scanner` process, VST3 format discovery, persistent catalog, timeout/quarantine and plugin ID resolution.
5. Instantiate, prepare and process one real VST3 plugin; add editor opening, bypass and reported latency, then capture/load its state.
6. Wire real control messages from WebView through validated control dispatcher to tested RT parameter bank, and route actual meter telemetry to UI.
7. Benchmark 64/128/256 samples on two Windows audio interfaces/drivers; report measured loopback RTL. Ship alpha as EXE/installer only after passing.

**Exit gate:** the user can sing into a mic, hear an actual VST3 effect and mute it, in a Windows executable with visible real meter, safe shutdown, no unexplained audio hang during test.

### P2 — YOUR SOUND (next session after P1)

- Implement multi-plugin chain, safe off-thread graph preparation/swap/retirement, plug-in editor lifetimes, bypass/reorder and latency display.
- Complete full per-VST3 state serialization, chain preset + mixer snapshot + session migration, missing-plugin placeholders and recovery.
- Connect preset/snapshot UI to real states; implement live-safe recall and audible A/B testing.
- Add music input, stereo master output, sends only when tested. Implement clear stream output solution, not a fictitious “virtual cable” dropdown.

### P3 — LIVE PERFORMANCE READY

- Packaging, crash report (opt-in, privacy), blacklisted plug-ins, hotplug reconnect, watchdog counters, native safety mute and 60-minute soak tests.
- UX review with first-time karaoke performers, accessibility, autosave/recovery, device setup instructions and demo video.
- Release pipeline: Visual Studio 2022 CMake CI; signed build when feasible, AGPL licensing notices, reproducible source archive, installer and user tutorial.

## 14. Prioritized risk register

| Risk | Severity | Early mitigation |
|---|---|---|
| WebView stalls and UI hides audio faults | High | Separate engine, native panic mute, heartbeat and telemetry |
| Third-party VST loads block/crash | Critical | Out-of-process scanner, quarantine; warn about in-process DSP limitations |
| Plugin internal lookahead makes singer monitoring delayed | High | Show summed plugin latency; Live profile refuses/warns on high-latency plugins |
| Race / use-after-free on chain reorder | Critical | Prepared immutable plans and deferred reclamation with ack |
| ASIO drivers differ across devices | High | Tested device matrix, explicit compatible driver selection and error messages |
| Output feedback and loopback | Critical | Monitor off by default, routing validation, one-click panic mute |
| Preset data silently loses plugin settings | High | Store full component+controller binary states, checksum/validate/migrate |
| Heavy UI effects slow rendering on integrated GPU | Medium | DOM/style profiling, reduce redraw rate, maintain reduced-motion mode |
| Copying proprietary mixer graphics | High | New vector/CSS elements and license review |
| Scope explosion before first usable live demo | High | P1 “one mic + one VST3 + output” non-negotiable exit gate |

## 15. Reusable code and code quality conventions

Repository modules have independent ownership and tests. Compile with C++20, strict warnings and clang-format, unit and regression tests for graph/state schemas. No global mutable state crossing uncontrolled thread boundaries. Every real-time function declares complexity bounds. No uncontrolled detached workers. Thread shutdown joins outside callback; OS/device teardown deterministic. Non-RT plugin binary state is size-capped and validated. Tests cover device initialization failure, graceful stop, plugin scan timeout, rapid parameter changes and graph swap stress. Telemetry logs never retain raw microphone recordings.

**Scaffold validation already completed:** `engine/core/RealtimeControl.h` provides one-producer/one-consumer bounded queue plus revisioned scalar parameter bank, and `tests/realtime_control_test.cpp` passed capacity, wraparound, 250,000 ordered interthread events, and last-value coalescing on Linux g++ C++20. These are primitives, *not* a realtime-safe plugin host guarantee.

## 16. References and design decisions

Primary technical documentation consulted:

1. JUCE WebView2 embedding / native bridge: https://juce.com/blog/juce-8-feature-overview-webview-uis/ and https://docs.juce.com/master/classjuce_1_1WebBrowserComponent.html
2. JUCE AudioProcessorGraph, prepare/rebuild contract: https://docs.juce.com/master/classjuce_1_1AudioProcessorGraph.html
3. JUCE licensing and ASIO configuration: https://github.com/juce-framework/JUCE/blob/master/LICENSE.md and https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/juce_audio_devices.h
4. VST3 SDK license: https://github.com/steinbergmedia/vst3sdk
5. VST3 plugin persistence: https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Persistence.html
6. VST3 latency accounting: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioProcessor.html
7. Steinberg ASIO SDK open-source license: https://github.com/audiosdk/asio/blob/main/LICENSE.txt
8. Microsoft Windows audio latency: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
9. JUCE plugin scanning crash discussion: https://forum.juce.com/t/vst3-plugin-scanning-crash-protection/58485

**Architectural decision records:**

- **ADR-001:** Live FX host as product category; not DAW, not generic digital soundboard.
- **ADR-002:** Windows-first JUCE native DSP + offline WebView2 UI; clear versioned bridge.
- **ADR-003:** FX Rack First with Easy/Pro modes, independently persisted user presets.
- **ADR-004:** Prepared immutable audio graph / no direct UI mutators in audio callback.
- **ADR-005:** Separate plugin discovery process; in-process DSP initially, optional sandbox later.
- **ADR-006:** ASIO and WASAPI baseline, device-specific measured RTL; no automatic clock mixing.
- **ADR-007:** Full binary VST3 persistence with safe snapshot staging and rollback.
- **ADR-008:** Open-source AGPLv3-compatible application, original visual assets, no proprietary plugin redistribution.

---

## Handoff: next exact step

Implement **P1 First Sound**. Start with native Windows audio device enumeration/pass-through and real peak meter, then add single VST3 plugin and state persistence. Do **not** start another decorative UI redesign before first sound. Keep this ARD and v0.3 screenshots as the accepted visual/architectural baseline; change UI only to support actual workflows or measured QA issues.
