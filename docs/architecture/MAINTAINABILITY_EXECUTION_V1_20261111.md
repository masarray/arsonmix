# ArSonMix Long-Term Maintainability — Executable Engineering Blueprint v1

**Status: IMPLEMENTATION PLAN / NOT A CODE FIX.** Re-audited 2026-11-11 at main 99f580d76bf20ea994d54f8916098c680897f41d. Issue: [#527](https://github.com/masarray/arsonmix/issues/527). Navigate through [architecture map](README.md), [fast feature playbook](FEATURE_EXTENSION_PLAYBOOK.md), and [next-thread execution prompt](NEXT_THREAD_EXECUTION_PROMPT.md).

## A. Outcome: faster feature delivery with LOWER future thinking cost

We want an agent who adds another built-in FX, VST3 control, channel command or Player feature to make a small, predictable, isolated change and its tests, without re-reading a 6,499-line application or rewriting the routing stack. Success is lower cross-module coupling, fewer special-case paths, one trustworthy ownership model, faster deterministic tests, and stable field audio. Splitting files just to make them shorter does NOT achieve the goal.

**What is explicitly out of scope:** rewriting from scratch; a new framework; replacing JUCE/WebView2; GUI restyling; renaming all code to satisfy a design diagram; a second graph/renderer/callback; making current hardware FAT claims; migrating all historical docs/tests in one giant PR; slowing P0 product work with a new approval gate.

**Hard invariants:** approved /preview/ assembled GUI only; single JUCE callback; one genuinely installed native ChannelId/EdgeId PCM authority; logical/offline state distinct; atomic graph prep/handoff; real device/pin identity and output authorization; RT nonblocking; .smix backward compatible; external VST3 lease/editor identity retained; no hidden MONITOR ritual on trusted settings; official smart CI policy.

## B. Second audit — concrete coupling map

| High-coupling path | Verified examples | Failure mode to prevent | Proposed owner |
| --- | --- | --- | --- |
| GUI command ingress | src/Main.cpp has handleLogicalMixerIntent, handleApprovedNativeControl, onParameter, applyCustomAudioDevice, FX-specific snapshot/edit methods and player commands | one more feature requires large host edits and risks unrelated device/callback logic | thin NativeCommandRouter + specific use-case services (extraction only) |
| Logical state | NativeLogicalMixerWorkspace copies candidate, validates, persists .smix, emits ACK; web/approved-native-bridge.js awaits later snapshot | offline edits accidentally treated as physical graph admission; partial commits or lost FX state | current MixerSessionDocument/Workspace/Store stay authoritative |
| Device/application lifecycle | Main.cpp applyCustomAudioDevice + LiveAudioEngine begin/finish/rebind + engine/device policies | repeated reset, duplicated Apply, stale pins/epoch, unintended output | one DeviceApplyCoordinator owned on CONTROL thread; no second AudioDeviceManager |
| Graph & callback | LiveDryRoutingLab, LiveAudioEngine, DynamicNativeSessionPublisher, RealtimeRoutePublisher | M4 compatibility silently competes with or downgrades installed ChannelId PCM | existing immutable publisher + single render-owner path, measured migration |
| Native status projection | src/ApprovedNativeV8Projection.h and integration/ApprovedNativeV8Projection.h share nativeV8Decimal/nativeV8Number but differ in installed/retained semantics | divergent validation, duplicate symbols if both included, stale/fake readback | one canonical validation/projection implementation with explicit mode |
| FX UI/host | Main.cpp has per-EQ/Comp/Harmony/Throw handlers; engine/builtin_fx has individual processors and shared FxSpsc | each new FX multiplies scattered branches | common host adapter for control/ACK/state/lease; each DSP stays specialized |
| Build & docs | approved UI assembled from internal seed + button overlay; 238 docs entries at top level | wrong GUI source or obsolete PR treated as current | canonical build provenance + short CURRENT architecture index |

**Do not exaggerate:** a large file is a risk signal, not proof of poor correctness. Real unit/PCM suites and path-scoped CI already exist; preserve them. web/styles.css's font data must not be counted as code complexity. First secure the semantics, then reduce coupling.

## C. Desired boundaries and dependency direction

~~~
GUI (one /preview/ renderer)
  UI event -> approved-native-bridge (translator only)
    -> NativeCommandRouter (validate request type/id/epoch/revision)
       -> MixerSessionUseCase -> MixerSessionDocument/Store
       -> RoutingUseCase -> existing native graph compiler/publisher
       -> FxHostUseCase -> existing FX registry/lease/adapters
       -> DeviceApplyUseCase -> existing JUCE DeviceManager/policies
       -> PlayerUseCase -> existing decode worker/virtual native source
    -> native ACK + later accepted snapshot (same authoritative sources)
  REALTIME: prepared immutable graph -> one JUCE AudioIODeviceCallback -> verified output
~~~

This is a dependency target, not an instruction to add all those classes now. Extract a new use-case owner ONLY if existing code already has a cohesive responsibility and doing so removes direct dependencies from Main, UI or the callback. Avoid pure pass-through classes and mirrored session models.

**Thread model**
- JUCE message/control: validate commands; device scans/open/close; native graph/FX preprepare; edit session; vendor editor/serialization coordination; emit ACK/readback.
- Background bounded workers: disk save, plugin scan, file decode, expensive analysis; never mutate installed PCM without control owner.
- Realtime callback: bounded deterministic DSP/render/read-only published graph and permitted atomics/SPSC only; zero allocation, lock, wait, filesystem, UI, log, plugin create/destroy.
- UI/browser: presentation/gesture staging only; no optimistic final state or independent routing authority.

**State and revision model**
- Document revision: accepted logical state, persists independently of hardware and survives restart.
- Device generation: real endpoint/driver/pin binding lifecycle.
- Installed PCM graph revision: preprepared and admitted topology, tied to actual device generation and output safety fence.
- Command requestId: deduplicated and correlated to ACK; snapshot for SAME accepted revision must subsequently confirm state.
- LOGICAL_ONLY / RETAINED / INSTALLED / AUDIBLE are different predicates; never infer later ones from former ones.
- Stale/missing/invalid request: explicit negative ACK, *accepted model and audio output unchanged*. Offline edits cannot ask plugin to run or render a live meter.

## D. Execution order: incremental, code-producing, with acceptance gates

### M0 (THIS documentation PR): navigable execution contract
Deliver docs/architecture/{README,MAINTAINABILITY_EXECUTION_V1_20261111,FEATURE_EXTENSION_PLAYBOOK,NEXT_THREAD_EXECUTION_PROMPT}, plus a short AGENTS.md pointer. Record baseline and issue #527. Docs only: inspect diff/links; no Windows/Chromium/FAT. **Does not close #527.** Next thread starts M1 coding immediately.

### M1 — One native projection contract + logical editor control boundary (FIRST CODE MILESTONE)

**Why first:** structural duplication and very high-value extraction without touching the ASIO render callback. The latest main already has logical-only PR #513; M1 makes that growth point cheaper for future channel/FX features.

**Affected sources to inspect before editing:**
- integration/ApprovedNativeV8Projection.h, integration/ApprovedNativeRuntimeSnapshot.h
- src/ApprovedNativeV8Projection.h and src/ConsoleLiveBridgeV6.h
- src/Main.cpp: logicalWorkspace methods (~1054-1340), approvedNativeSnapshot/handleApprovedNativeControl (~1320-1540)
- src/NativeLogicalMixerWorkspace.h, src/NativeMixerSessionDocument.h, src/NativeMixerSessionStore.h, src/NativeMixerSessionSaveWorker.h
- web/approved-native-bridge.js and web/approved-native-contract.js (contracts only; avoid repaint/rewrite)
- tests/approved_native_v8_projection_test.cpp; tests/native_mixer_session_store_test.cpp; tests/milestone_a_single_authority.test.cjs; tests/milestone_a_native_contract_test.mjs; tests/approved_native_offline_logical_smoke.py.

**Implementation**
1. Inventory exact callers/definitions. Canonicalize shared decimal/number validation and projection semantics to ONE implementation with explicit installed/retained mode. Preserve the public function signatures through *thin forwarding compatibility only where called*; never ship two copies of the same function body or introduce header ODR collisions. Only installed PCM may set callbackOwnsChannelIds true. Retained and logical-only remain nonlive. Test rejection is mutation-free.
2. Extract the coherent logical edit/ACK/save unit from ConsoleWindow into a control-thread service that owns the *existing* NativeLogicalMixerWorkspace and coordinates the *existing* session store/worker. Keep one document, one commit/rollback, one correlated ACK and later native snapshot. Service may receive narrow injected references/callbacks; do not duplicate engines, plugin registry, rendering or browser models.
3. Leave Main.cpp responsible for creating one service, wiring JUCE events and forwarding results. Do not move whole file, device setup, or complicated unrelated editor/ASIO lifetimes as a "cleanup".
4. Ensure per-feature logical FX catalog validation, processor state capture and queued save remain correct for EQ, Compressor, Harmonice and Vocal Throw; preserve vendor VST3 state and read-only recovery. Control only: never capture arbitrary vendor state in callback.
5. Update tests that search exact source expressions only if the implementation legitimately moves; add behavior-level regression exercising the new service or same boundary. Do not weaken assertions. Preserve GUI source/hash parity.

**M1 acceptance**
- Same valid/invalid installed/retained projections, monotonic revisions, zero snapshot mutation on rejects; no fake live meter or callback admission.
- Offline zero-device ADD/RENAME/REMOVE channel, BUS/WIRE, FX reference/bypass and .smix roundtrip: ACK + independent later readback. Duplicate/stale/busy/write-failure and recovery reject safely, with previous accepted document intact.
- Physical graph path remains untouched; compiled Windows native tests and browser-integration smoke qualified at latest exact SHA if C++ + bridge contract changed.
- Demonstrable shrinkage in ConsoleWindow's direct knowledge of logical-edit persistence/validation, not a size-only rename. New FX has one clear registration/intent seam.
- No prior approval required; one coherent PR, branch reconciled with active #512, no duplicate UI/PCM callback.

### M2 — Device lifecycle boundary (after M1, coordinate with product P0)

**Files:** src/Main.cpp applyCustomAudioDevice/showDeviceSetup, src/LiveAudioEngine.h beginManualAudioSetup/finishManualAudioSetup/rebind, engine/device/{DeviceSessionTransitionPolicy,AsioRestartRecoveryGate,EndpointIdentityPolicy,...}, src/NativeMixerSessionDocument.h.

**Work:** own one idempotent device apply/rollback coordinator with explicit device generation, pin identity preflight, safe output gate, snapshot-before-apply, durable logical-session preservation and asynchronous/nonblocking progress. Do NOT reinstantiate DeviceManager or introduce a second audio callback. Setup panel opening must not reset session. Rejected driver open never overwrites accepted mixer. A known verified device should restore intended routing without extra user MONITOR/ARM ritual, while unknown output stays safely unbound pending one contextual choice.

**Tests:** tests/device_independent_mixer_session_test.cpp, tests/device_session_transition_policy_test.cpp, tests/asio_restart_recovery_gate_test.cpp, tests/native_mixer_session_store_test.cpp, tests/approved_native_cold_start_smoke.py, relevant real hardware FAT. Confirm offline edit while no device, MAIN pin identity, NONE->MONO->STEREO, buffer/rate restart, failed apply/rollback and stream isolation.

**Exit:** one guarded device transaction and no duplicate apply UI / reset entry points; retained graph/FX/session exact. Hardware FAT PENDING until witnessed, never claim green CI is physical pass. This milestone must not collide with active #512 or #412 work.

### M3 — Production graph and M4 compatibility retirement (ONLY with direct PCM proof)

**Files:** src/LiveDryRoutingLab.h, src/LiveAudioEngine.h, engine/core/{DynamicNativeSessionPublisher,RealtimeRoutePublisher,DynamicPreparedMixerBlock,UnifiedLegacyProductionAdmission,LiveChannelFxGraph}. 

1. Trace every actual render producer/fallback in the installed callback; write a call graph and counters for which path is exercised. Preserve CH1 vendor lease, admitted ChannelIds, ASIO output safety and playback.
2. Prepare a minimal behavior-preserving removal of one *proven dead* compatibility branch at a time. No blanket delete of M4/Lab APIs while they are still used. Do not enable legacy fallback when native graph admission fails.
3. Regression: any HW IN->CH05->FX->BUS FX->MAIN; SOURCE ONLY with nonzero channel meter and silent MAIN; WIRE re-target/remove/reconnect; parallel sends without unintended duplicate; FX slot reorder/remove/editor instance identity; mono/stereo; device stop/rebind. Include block-by-block golden PCM and startup/recovery failure paths.
4. Stress callback CPU and PDC; no heap/lock or vendor destruction, no device thread blocking due to WASAPI stream.
5. Exact-SHA Windows/native/FAT per smart policy, real physical listening remains independent.

**Exit:** fewer competing branches and one proven PCM authority; no behavior regression. If product audio blocker remains, prioritize its root fix over cleanup. Do not silently change plugin sound.

### M4 — Standard FX-host extension seam

**Files:** src/FactoryFxRegistry.h, src/PluginCatalog.h, engine/builtin_fx/{BuiltInFxCatalog,ChannelEqProcessor,ChannelCompressorProcessor,ChannelHarmonyProcessor,VocalThrowProcessor,FxSpsc}.h, engine/core/{DynamicChannelFxLeaseBank,RealtimeNativeFxControl,LiveChannelFxGraph}.h, native FX request handlers in Main and web approved adapter.

1. Keep unique DSP code per plugin; consolidate host *plumbing* only: catalog identity, stable slot ID, processor lease/editor ownership, input/output layout, prepared state, bounded parameter update + ACK, bypass/rack bypass, state versioning, PDC, meter readback.
2. Prove by registering a lightweight test processor through the same extension path as existing built-ins WITHOUT new plugin-specific branching across unrelated device, routing and GUI files. A production plugin is not required in the first structural fixture.
3. VST3 and built-ins share host lifecycle rules but NOT necessarily identical UI or DSP; genuine vendor GUI remains legitimate. Zero changes to DSP algorithms unless specified separately.
4. Tests: tests/live_channel_fx_graph_test.cpp, tests/native_mixer_session_store_test.cpp, tests/factory_fx_registry_test.cpp, targeted per-effect DSP tests, FX editor and stored-state browser smoke. Unavailable VST3 preserves placeholder/state, does not erase rack.

**Exit:** documented recipe with predictable touched files; no new host-wide switch statement for every FX.

### M5 — GUI native adapter reduction (UI only, preserve assembled authority)

**Files:** web/approved-native-bridge.js, web/approved-native-contract.js, web/mixer-io.js, web/monitor.js, scripts/build_approved_native_gui.py, scripts/build_pages.py.

- Identify script *actually packaged* into /preview/ first; archived web/app.js/web/bridge.js do not automatically mean another shipped GUI.
- Isolate typed intent serialization, ACK correlation and snapshot reconciliation as pure testable modules. Keep one DOM/view model (same scope) and one native subscription; split *by coherent domain*, not file length.
- Never create a shadow local graph, browser audio, extra subscription or a second device setup UI. UI shows pending vs accepted correctly and visibly responds to APPLY.
- Verify approved SVG/PNG assets, native caption, FX picker, WIRE gesture, bank/viewport geometry, pointer and hardware setup; tests/approved_native_m2_contract.test.cjs plus real Chromium smoke.
- Exit: a new native command has one documented handler and predictable GUI seam, with no duplicate listener.

### M6 — Guardrails, documentation, and maintenance hygiene (incremental with M1-M5)

- Keep this map updated when paths truly move; use ADR states CURRENT / IMPLEMENTED PARTIAL / SUPERSEDED / HISTORICAL (older docs stay available for archaeology).
- Add only *cheap*, measurable static checks if a regression demonstrates need: duplicate contract/handler ownership, forbidden callback operations or unexpected extra PCM source, inactive alternate GUI bundle, schema serialization compatibility. Prefer target-specific over broad regex false positives.
- Add C++ warnings/static analysis on changed modules when toolchain/profile permits, without blanket -Werror on vendor code. Consider deterministic clang-format conventions on touched code and a small JS formatting boundary; do not massive-reformat GUI.
- Preserve current path-scoped fast CI and exact-SHA milestone jobs. Do not add duplicate Windows/Chromium jobs or require owner to click Actions.
- No external dependencies or autogenerated abstraction frameworks without proving they remove real maintenance work.

## E. Fast validation matrix (commands are from repository)

| Change | Cheapest relevant check (choose actual impacted set) | Milestone qualification |
| --- | --- | --- |
| Projection + native session | CMake C++ test targets registered in CMakeLists.txt for approved native V8 and session; node --test tests/milestone_a_single_authority.test.cjs tests/milestone_a_native_contract_test.mjs | one native MSVC/CTest at exact candidate SHA |
| GUI native bridge | node --test tests/approved_native_m2_contract.test.cjs tests/gui_workflow_contract_test.mjs; approved GUI bundle source checks | one UI Chromium milestone at exact SHA if semantics/interaction changed |
| Graph/routing/FX lifecycle | relevant CTest (routing, FX, PCM, session, PDC), deterministic PCM before/after fixtures | one native or FAT (not both), plus UI only if changed |
| Device/physical output | device policy/ASIO restart and session tests; no fake playback assumptions | FAT package + independent real ASIO listening and failed-device recovery evidence |
| Documentation alone | inspect links, filenames and diff; no source build required | NO CI milestone; do not dispatch Windows |
| Release packaging change | specific installer/packaging tests; source provenance | release controller, exact artifacts and integrity separate |

Target a *single* coherent feature PR; run cheap relevant tests while editing; read actual CI status; request /arsonmix-ci ui|native|fat <exact PR HEAD SHA> only for a justified milestone using the authenticated PR top-level comment. FAT includes native MSVC + CTest and artifact, so never request native + fat for the same SHA. A qualified ZIP is still NOT physical listening proof.

## F. Dependency/coordination strategy

- main audit baseline: 99f580d76bf20ea994d54f8916098c680897f41d; always re-read current main before execution.
- Active draft #512 touches Main, LiveAudioEngine, LiveDryRoutingLab, native bridge, Harmony, session. Do NOT overwrite its HEAD or mass cherry-pick. Its work may merge after this document, so rebase/reconcile by changed-file semantics.
- Active draft #521 is experimental Harmonice latency; do not fold unqualified DSP study into architectural cleanup.
- #490 (cold-start logical editing), #412 (physical routing/FAT), #192 (dynamic mixer), #15 (crackle) retain product defect ownership. This plan does not close or supersede them.
- Build a scoped new branch for M1 based on LIVE main; one integrated PR for projection+logical-control boundary if both safe. If concurrent P0 overlap blocks one slice, complete a cohesive non-overlapping verified seam and record the exact deferred dependency; do not ask for another design approval or create a chain of empty planning PRs.

## G. Engineering acceptance metrics (measure, do not invent baseline)

For each implementation PR record BEFORE vs AFTER: modules touched for a normal feature, domain responsibilities located in Main, duplicate producer/validator count, complete test suite names/results, code paths invoking callback or device setup, compilation/test overhead if measurable, and actual audio lifecycle invariants. Future aspirational goal: adding a standard built-in FX primarily changes its DSP/registry/parameter/editor files and tests, not unrelated device/Main routing special cases. Quantify reductions only after diff inspection; line count alone is never a pass/fail criterion.

**STOP/ROLLBACK:** if an extraction changes rendered PCM, plugin lease lifetime, accepted .smix, host physical identity, output safety or approved native GUI, revert that behavior slice and diagnose root cause instead of adding a temporary alternate path. Never compensate for a failed test by weakening it.

## H. Deliverable protocol — every coding agent

Report: exact repo/main/PR branch+head SHA; actual files and responsibilities removed/added; behavior unchanged or intentionally changed (specify); deterministic quick tests with evidence; exact one selected expensive lane and run URL or reason skipped; physical FAT PASS/FAIL/PENDING separately; concurrency conflicts; what specifically remains for next milestone. Do not claim a future background job or an unverified release.

**FIRST NEXT ACTION:** [Use the complete new-thread execution prompt](NEXT_THREAD_EXECUTION_PROMPT.md) to code M1, not create another strategy-only document.
