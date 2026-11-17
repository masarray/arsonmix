# ADR — ArSonMix Modular Monolith + MVVM Feasibility (Not a Rewrite)

**Date:** 2026-11-18 · **Decision:** CONDITIONAL YES to MVVM/PRESENTATION MODEL for the WebView2 UI; NO to converting the entire audio engine to MVVM.  
**Audited main:** 85d84195d06c8798ce3a4309b362766c13b07e14  
**Status:** architectural feasibility and staged execution contract, no runtime rewrite performed.  
**Related:** [full multi-I/O ARD](../ARD_ARSONMIX_MULTI_DEVICE_IO_V2_20261118.md), [research](../RESEARCH_ARSONMIX_MULTI_IO_MVVM_20261118.md), [existing maintainability plan](MAINTAINABILITY_EXECUTION_V1_20261111.md), [issue #527](https://github.com/masarray/arsonmix/issues/527), [approved GUI](https://masarray.github.io/arsonmix/preview/).

## A. Source-grounded diagnosis: why new features have high regression risk

- **Main.cpp (8,873 lines)** currently hosts WebBrowser shell and numerous native command, device setup/recovery, FX editor and control/UI integration methods. The critical issue is *mixed ownership across paths*, not raw file size. Large refactors that move 8k lines without changing dependencies add risk but little value.
- **LiveAudioEngine.h and LiveDryRoutingLab.h (~3,637 lines each)** carry device lifecycle and native renderer, plus session binding, routing and compatibility; a feature touching device identities easily intersects renderer state. The strongest remedy is an explicit *Device I/O transaction boundary*, not a second active native audio engine.
- **approved-native-bridge.js (4,283 lines), monitor.js (2,118), mixer-io.js (2,082)** intertwine native event transport, accepted/pending state, view updates, and route gesture handling. A testable presentation state projection/ViewModel has high value **without** changing HTML/CSS/approved /preview/ geometry.
- **Important existing improvements:** NativeLogicalMixerControl, NativeDeviceApplyCoordinator, DynamicNativeSessionPublisher and console-v8 contract have begun separation. Issue #527/M1-M6 contain already merged work and partial gaps. Do NOT duplicate these classes under fresh MVVM names.
- The UI already has a simulated browser preview and a native WebView2 host. Browser-only LIVE labels are not installed physical PCM proof. Any ViewModel must derive those statuses from accepted native readback.

**Acknowledgement:** The codebase is presently costly to modify consistently. The best solution is a measurable, incremental separation of UI presentation, use-case control, domain model, infrastructure adapters and realtime render authority—not a large one-time 'convert to MVVM' commit.

## B. Options comparison

| Option | Benefits | Risks | Decision |
| --- | --- | --- | --- |
| Full WPF/WinUI/C# MVVM rewrite | Mature bindings/tooling | Abandons JUCE/WebView2/approved UI; interop and VST3 native complexity; major regression | REJECT |
| Full React/Redux rewrite and new GUI | Off-the-shelf state patterns | Huge UI parity/asset/hotkey risk, duplicate source of truth, rendering costs | REJECT as initial migration |
| Keep native C++/JUCE, add **JS/TypeScript presentation ViewModels** for existing DOM | Separates state/gestures from DOM, testable pure transforms; smallest visual risk | Requires native state/ACK discipline, bundle parity | RECOMMEND |
| Native C++ MVVM model for whole mixer DSP | Uniform naming | Misapplies UI pattern to realtime/render; unsafe observer notifications in callback | REJECT |
| **Modular monolith** with application use-case services + bounded domain/RT adapters | Single accepted graph, testable device/session commands, maintainable extension | Requires incremental ownership extraction, no immediate big visual change | RECOMMEND in parallel |
| Add more thin wrappers around Main and every handler | Small apparent files | Multiple pass-through classes, duplicated command paths, no improvement | REJECT |

Official basis: [Microsoft MVVM](https://learn.microsoft.com/en-us/windows/apps/develop/data-binding/data-binding-and-mvvm), [Fowler Presentation Model](https://martinfowler.com/eaaDev/PresentationModel.html), [JUCE 8 WebView2 native integration](https://juce.com/blog/juce-8-feature-overview-webview-uis/), [Redux state flow](https://redux.js.org/understanding/thinking-in-redux/three-principles). These are architectural precedents, not instructions to introduce a new runtime framework.

## C. Target dependency direction (one owner for each domain)

    VIEW: approved /preview/ DOM + input/faders/FX/WIRE controls
       | user gesture; no own PCM authority
       v
    VIEWMODEL: pure JS/TS selectors, local pending state & status projections
       | typed operation + requestId + expected revisions
       v
    TRANSPORT: existing JUCE WebView2 native bridge (one dispatcher)
       | no UI rules or DSP
       v
    APPLICATION: existing native session/routing/FX/device use-case owners
       | validate / stage / prepare / publish / correlate readback
       v
    DOMAIN: durable .smix session, ChannelId/EdgeId/FX identity,
            SourceBinding/OutputBinding, graph and resource policies
       | immutable accepted snapshots
       v
    INFRASTRUCTURE: JUCE ASIO/WASAPI device adapters, persistence, VST3
       | CONTROL-only device and plugin lifecycle
       v
    REALTIME: one accepted ChannelId PCM/FX render graph
              + independent capture-only and output-only PCM I/O
              + bounded SPSC/ASRC; no object/observer UI graph

The UI ViewModel is **not** another authoritative mixer model. C++ accepted logical/installed snapshot is the truth. UI ViewModel may contain a pending local draft, selection/focus and animations; it must always reconcile with matching requestId/epoch/graphRevision/readback. UI may optimistically animate a pressed button but may NOT claim an accepted audio route, hardware device or LIVE state until native readback.

No message from a native audio callback may synchronously notify Vue/React/DOM. Hardware telemetry should be preaggregated on control/status thread and sent at a bounded UI cadence; faders/VU require separate lightweight channels/selectors, not a full application/snapshot rerender per meter sample.

## D. Concrete component ownership (reuse not rebuild)

| Area | Current owner | Safe incremental extraction when tests prove value |
| --- | --- | --- |
| App shell | src/Main.cpp ConsoleWindow + Browser | Remains composition root / JUCE lifetime; strip domain logic from handlers; do not move window resources to JS |
| Native event bridge | WebBrowserComponent + approved-native-contract.js / bridge.js | One NativeCommandRouter / typed request validation; **reuse** existing controller service where available |
| Logical document | NativeLogicalMixerControl / Workspace / Store | Remains authoritative .smix, FX vendor states and logical edits |
| Audio device lifecycle | NativeDeviceApplyCoordinator + LiveAudioEngine | One serial IoTopologyApplicationService for primary + optional secondaries; do not duplicate primary apply |
| Routing+FX admission | LiveDryRoutingLab + DynamicNativeSessionPublisher | Immutable accepted graph and one render owner, explicit FX lease; bounded extraction of policy from callback |
| Read model | Current approved native projection / console contract | One versioned ConsoleReadModel, composed outside realtime from canonical installed snapshots |
| UI state | approved-native-bridge.js, monitor.js, mixer-io.js | Pure DeviceSetupVM, RoutingVM and Fader/FX VM selectors, without new global JS accepted session |
| Rendering | approved /preview/ HTML + CSS + PNG controls | Keep same DOM, preserve exact visual baseline, patch only affected selectors on state changes |

**Rule:** naming a new class with 'Service', 'ViewModel', 'Manager', 'Controller' or 'Coordinator' provides no benefit unless it takes ownership away from an existing function, reduces dependencies, has one test boundary and removes old implementation paths. Avoid expanding architecture as a hierarchy of wrappers.

## E. Versioned interaction/readback contract

Existing console-v8 wire contract provides requestId, deviceEpoch, baseGraphRevision, baseRosterRevision, operation, payload and ACK. Preserve canonical uint64 decimal strings; do not silently convert IDs to imprecise JS Number.

For ViewModel:
- **Accepted state:** derived from native read model with matching accepted device epoch + graph revision (for installed PCM, also installed owner and output permission).
- **Draft state:** scoped to one UI interaction, can be reverted/discarded and must not alter native installed session before ACK.
- **Pending state:** requestId/timeouts with deterministic stale/reconnect handling; timeouts are NOT implicit success.
- **Error state:** typed fault with recoverable next step; preserve previous accepted route and physical device.
- **Telemetry state:** independently sampled/capped; never the audio command or session version.
- **SourceBinding and SinkBinding:** native-qualified device generations and pins, with logical intent retained when physical state offline.

Domain changes happen only in application service transactions, not in UI reducers. A native ACK without later matching installed graph readback is not proof of audio. A browser test mock must show simulation mode explicitly.

## F. Incremental migration plan (strangler, not rewrite)

Each stage ships a cohesive vertical slice with tests, rollback and no global behavior alteration:

| Stage | Specific code change | Proof / stop condition |
| --- | --- | --- |
| MV-0: dependency inventory | Record current method/data ownership, command propagation, output authorization and extension metrics from this main | Audit linked to current source and tests; no new framework |
| MV-1: one native command slice | Extract exactly one **Audio Device apply/readback** path out of Main into existing NativeDeviceApplyCoordinator/native application service, moving business logic rather than copying it | Same existing native command/ACK/device epoch and restart behavior; previously compiled Windows/CTest regressions pass; Main handler becomes dispatch only |
| MV-2: first presentation ViewModel | Extract Device Setup projection and pending/rejected/accepted lifecycle from mixer-io.js to a pure JS/TS module; retain exact DOM and approved /preview/ | Pure reducer/selector tests incl stale requests, plus Chromium/natively assembled GUI parity |
| MV-3: native read model | Consolidate duplicate native status sources into single canonical versioned snapshot, with one readback source and downstream selector | No JS-local LIVE claims; regression output round-trip and source picker behavior |
| MV-4: routing ViewModel | Move routing source/OUTPUT picker transient state from approved-native-bridge.js/monitor.js to small pure selectors; native binding and ACK unchanged | SOURCE ONLY/WIRE/BUS/MAIN snapshot+UI tests, no duplicate controllers |
| MV-5: device/FX boundaries | Integrate planned multi-device IO control path after IO-1 probe; remove redundant compatibility branch only with PCM proof | One graph + FX leases, status/driver failure isolated, .smix intact |
| MV-6: optional TS and tooling | Type existing JS command/read model from JSON contract, build repeatably into the one actual /preview/ bundle, add dependency/lint boundary checks | No new frontend runtime required; existing approved PNG/buttons/fader responsive and identical |

**Crucial ordering:** M1–M6 in the existing maintainability roadmap are NOT to be redone. These MV stages identify remaining gaps at current main. An MVVM sweep must not alter GPU rendering, plugin voicing, native recorder, routing schema and device manager all at once. First use device setup as a small vertical test seam; then expand only if measurable.

## G. Engineering fitness checks and measurable success

Do not set arbitrary improvement percentages with no baseline. Establish current metrics in CI and then compare each slice:

- **Ownership:** number of separately writable canonical mixer/device state stores (target exactly one accepted native owner per domain); count paths that mutate installed graph (only authorized native service).
- **Feature change surface:** record number of architectural layers/files touched for one representative operation, and which edits are actually required. Fewer unrelated modules should change; do not pretend a smaller file always means less coupling.
- **CI:** native PCM, command ACK/readback, .smix migration and exact native WebView2 GUI smoke remain green; no unjustified blanket test run for documentation changes.
- **Realtime:** no extra DSP graph, no two VST3 processing threads, no heap/lock/OS/API/JS callback work, no extra FIFO on primary direct ASIO mode.
- **State truth:** after device lost/reopened: logical controls unchanged, stale device invalidated, route LIVE only after native installed readback; no input / output phantom pin.
- **Interface fidelity:** CSS/PNG layout, event behavior, meter cadence and responsive /preview/ remain identical except user-approved additive I/O controls.
- **Time to extend:** sample another plugin or route command before/after extraction; fewer duplicated validation paths, simpler tests and less unrelated source reading (record measurements, not estimates).

## H. Anti-patterns / explicit no-go

NO full WPF/WinUI move, NO wholesale React rewrite, NO second JS mixer engine, NO C++ realtime observer bindings, NO one ViewModel per PCM sample, NO generic app-wide event bus from callback, NO plugin FX state copied into JS authoritative store, NO duplicate native request handler, NO action that tears down approved mixers on opening device settings, NO speculative class-per-action proliferation, NO reimplementing completed M1–M6 maintainability fixes.

The approved GUI is **/preview/**, not /preview-v2/. Current AGENTS.md and architecture/README.md override historical mock snapshots. ArSonMix is the mixer product; Harmonice remains a plugin namespace only.

## I. Feasibility verdict and risks

**MVVM for UI: HIGH feasibility** with bounded, incremental extracted ViewModels and no new UI framework required. **C++ application service/domain separation: HIGH feasibility** but requires deterministic integration tests and explicit ownership extraction. **Rewriting whole native DSP, hosting and GUI under MVVM: LOW value / HIGH risk**, rejected.

Combined with the multi-device ARD, these changes reduce future maintenance cost only if each stage removes old logic and proves unchanged behavior. Refactoring by rename/file split alone is not progress. Before release, independently qualify actual Windows multi-device ASIO support, output sink timing and plugin bypass/session retention; MVVM cannot replace hardware FAT.

**Source reference:** [issue #527](https://github.com/masarray/arsonmix/issues/527), [current architecture map](README.md), [existing execution blueprint](MAINTAINABILITY_EXECUTION_V1_20261111.md), [full I/O PRD](../PRD_ARSONMIX_MULTI_DEVICE_IO_20261118.md) and [global research](../RESEARCH_ARSONMIX_MULTI_IO_MVVM_20261118.md).
