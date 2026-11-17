# ADR — Scalable Modular MVVM + One Realtime Audio Authority (2026-11-18)

**Status:** PROPOSED architecture / source-grounded migration plan; **NOT implemented or merged by this document**.  
**Audited source:** `main@ef7a29933109020657accfb957b7a6ffc8d58cd7` on 2026-11-18. Re-fetch live main and active PR heads before implementation.  
**Parent:** [maintainability epic #527](https://github.com/masarray/arsonmix/issues/527).  
**Binding constraints:** root [AGENTS.md](../../AGENTS.md), the single assembled public `/preview/`, accepted sound/session identities, actual native ChannelId/EdgeId PCM authority, and current protected-main rules.  
**Companion:** [Parallel Thread Convergence Protocol](PARALLEL_THREAD_CONVERGENCE_20261118.md). Existing [architecture map](README.md) and [feature playbook](FEATURE_EXTENSION_PLAYBOOK.md) remain useful; this ADR supersedes neither already accepted product contracts nor measured runtime evidence.

## 1. Decision

Adopt **a modular monolith in one application process**, with:
1. **MVVM / Presentation Model only at the GUI boundary**: View = existing WebView2-approved assembled DOM; ViewModel = typed, observable, presentation-only projections of accepted native state. Never create a second logical session or native audio engine in JavaScript.
2. **Application use cases behind explicit ports**: routing, session, device, FX host/editor, Player, presets. All mutations remain native CONTROL/MESSAGE-thread transactions; `src/Main.cpp` becomes composition/startup/transport, not a second domain service.
3. **Domain model**: the existing accepted MixerSessionDocument, stable ChannelId and EdgeId, state migration, business constraints, requested-versus-accepted revisions. Do not mirror this into another permanent model.
4. **Realtime kernel**: retain one JUCE render callback, validated/prepared immutable ChannelId PCM graphs, bounded atomic publication/retirement, explicit physical output authorization and separated secondary devices. No GUI/service code on callback.
5. **Contract-driven product extensions and CI**: typed/versioned intent and snapshot contracts, central FX registration, release-profile manifest, architecture tests, exact-head CI, and separately witnessed hardware FAT.
6. **Single product integration owner, multiple parallel domain contributors**: GitHub main + PR head + reviewed diff/contract is the live source of truth. See companion protocol.

This is NOT a rewrite to WPF, C#/.NET MVVM, React, Qt, microservices, a new JUCE graph, a second AudioDeviceManager, a JS DSP engine, or a second `/preview/` renderer. Choose abstractions only where they eliminate a measured coupling; no empty one-method wrapper forest.

## 2. Why this combination, rather than pure MVVM

MVVM addresses UI/presentation separation, not audio scheduling, source of session truth, device epochs, or plugin processing. Hexagonal/ports-and-adapters isolates application policies from changing UI, drivers, filesystem and external plugins. A modular monolith retains low-overhead in-process graph handoff and predictable ABI while allowing many independently testable feature areas.

Publicly inspectable benchmarks (NOT claims that ArSonMix duplicates proprietary DAW internals):
- **Ardour** exposes concepts such as Session, Route and Processor, with UI and processing engine separated in public source documentation: https://ardour.org/files/doxygen/
- **Tracktion Engine** is delivered as JUCE modules with an Edit/Engine distinction and an independently evolving processing graph: https://github.com/Tracktion/tracktion_engine and https://www.tracktion.com/develop/tracktion-engine
- **JUCE** documents the AudioIODeviceCallback as a high-priority audio thread; `AsyncUpdater::triggerAsyncUpdate` can block and is **not** a safe shortcut from realtime audio: https://docs.juce.com/master/classjuce_1_1AudioIODeviceCallback.html and https://docs.juce.com/master/classjuce_1_1AsyncUpdater.html
- **Steinberg VST3** separates process-time parameter queues and plugin processing from controller/state management; process must avoid allocating: https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/API%2BDocumentation/Index.html
- **Presentation Model/MVVM**: https://martinfowler.com/eaaDev/PresentationModel.html ; **Ports and Adapters**: https://alistair.cockburn.us/hexagonal-architecture/
- **DORA** links loosely coupled architecture and small batches to reliable independent development: https://dora.dev/capabilities/loosely-coupled-teams/ and https://dora.dev/capabilities/trunk-based-development/
- **GitHub** required checks, CODEOWNERS and optional merge queue: https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/managing-a-merge-queue

Do not infer verified private Ableton, Cubase, REAPER or Bitwig internals from marketing material; use their documented behavior for UX benchmarking only.

## 3. Current audited seams and risk

| Concern | Existing concrete owner / entry point | Present risk | Target |
| --- | --- | --- | --- |
| Native composition + WebView transport | `src/Main.cpp` / `ConsoleWindow` | GUI/device/FX/Player logic in one large class | Composition + thin request routing |
| Native GUI bridge | `web/approved-native-bridge.js`, `web/approved-native-contract.js` | UI projection, transient gesture handling and domain-specific glue mixed | Typed feature ViewModels; thin native transport |
| Logical mixer | `src/NativeLogicalMixerControl.h`, `NativeLogicalMixerWorkspace.h`, `NativeMixerSessionDocument.h` | Distinct logical/installed truth may be conflated by callers | One authoritative document + transactional use cases |
| Graph publication | `engine/core/DynamicNativeSessionPublisher.h`, `DynamicPreparedMixerBlock.h` | State/version/device binding and retirement complexity | Retain immutable publication contract; test boundaries |
| Callback and device | `src/LiveAudioEngine.h`, `engine/device/**` | ASIO lifecycle, source identity, secondary output and VST lifetime coupled | Device service; uninterrupted one render owner |
| Legacy compatibility | `src/LiveDryRoutingLab.h` | M4 acceptance and modern ChannelId PCM still interact | Parity-proven strangler retirement, NEVER blind delete |
| FX plugins | `src/NativeBuiltInFx.h`, `engine/builtin_fx/**` | A new FX may need scattered special cases | One descriptor/instance/state/editor contract |
| Build and installer | `CMakeLists.txt`, `p1-windows.yml`, `alpha-preview-publish.yml` | Feature flag mismatch previously omitted ImmerSIB in a FAT candidate | Product-profile/compiled-catalog parity checks |

A large file is an indicator of coupling, not automatic proof of correctness failure. `web/styles.css` includes large embedded font data; exclude those bytes from rule complexity estimates. Preserve already working M1–M6 extraction and comprehensive regression suites.

## 4. Intended runtime boundaries

```text
ONE approved /preview/ DOM (VIEW)
    -> Feature ViewModels: Channel, Routing, FxRack, Device, Player, Session
       (UI formatting, pending gestures, validation hints, read-only projections)
    -> typed native transport / versioned envelope (not a second state owner)
    -> CONTROL-thread CommandRouter (request ID, permissions, revision, epoch)
       -> RoutingUseCase -> existing MixerSessionDocument + graph compiler
       -> FxHostUseCase -> existing built-in/VST3 registry, lease and editor
       -> DeviceUseCase -> existing JUCE device apply/recovery
       -> SessionUseCase -> existing .smix store and background save worker
       -> PlayerUseCase -> existing Player source/transport
    -> prepare graph off callback -> immutable revision/epoch publication
    -> ONE JUCE audio callback -> physical output
    -> accepted logical/installed status + error codes + native snapshot
    -> ViewModel projection -> same approved VIEW
```

Control is not a new permanently blocking worker or second command bus. Existing native control/message ownership remains until a bounded extraction proves equivalent. `*UseCase` names are logical responsibilities, not a mandate for one new class per command. Preserve the one accepted `.smix` schema/owner; adapting a view does not entitle it to commit session state.

### Thread and data ownership

| Execution context | Permitted | Prohibited |
| --- | --- | --- |
| WebView/UI | Render, gestures, pending presentation, coalesced meters | Claiming PCM LIVE from DOM, saving independent mixer truth, direct DSP calls |
| Native message/control | Validate commands, plugins instantiate/prepare, immutable graph compile, device apply, ACK/readback | Long waits on callback, ignoring stale request/epoch |
| Bounded worker | Scan/decode/file save/expensive analysis | Unsynchronized mutation of installed graph |
| Audio callback | Deterministic bounded process, atomic/snapshot loads, preallocated buffers/SPSC | Heap allocation, mutex wait, filesystem, UI calls, logging, editor create/destroy, unsafe retired graph disposal |

VST3 parameter automation and plugin state must respect host/plugin threading, reported latency and lease lifetime. No undocumented assumptions of arbitrary vendor thread safety. Prevent direct `AsyncUpdater` from callback: post only via a demonstrably bounded realtime-safe handoff to non-RT code.

### One transaction / state envelope (design, not yet a deployed ABI)

```json
{
  "schema": "armix.native.intent.vNext",
  "requestId": "unique-user-operation-id",
  "target": { "channelId": "exact-decimal", "edgeId": "exact-decimal-or-null" },
  "expected": {
    "documentRevision": 120,
    "deviceEpoch": 9,
    "installedGraphRevision": 77
  },
  "action": "SET_SEND_TAP",
  "payload": { "tap": "PRE_FADER" }
}
```

Do not require a physical device epoch for a purely offline logical edit. An accepted logical edit means `LOGICAL_ONLY`; an installed graph ACK means accepted revision + actual physical device/pin; `AUDIBLE` requires separate output authorization and actual signal flow. Use a correlated, bounded command-result/status ledger and later accepted snapshot; rejected/stale commands change neither accepted document nor audible graph. Keep stable ChannelId/EdgeId and vendor VST3 state across migration. Evolve existing contract with compatibility adapters, not a hard vNext cutover.

## 5. Module map for incremental migration

**Proposed conceptual modules; folders below are TARGETS, not a demand to move all current files immediately.**

| Module | Source ownership | Allowed dependency |
| --- | --- | --- |
| `presentation/mixer` | existing assembled WebView assets, future focused ViewModels | `contracts` and browser bridge only |
| `application/routing` | current `NativeLogicalMixerControl`/Workspace transactions | domain IDs/session + graph compiler port |
| `application/fx` | generic host leases, state/editor actions | FX registry and prepared graph ports |
| `application/device` | native apply coordinator | device capability/readback adapter |
| `application/player` | existing native player control | native Player source and accepted session port |
| `domain/mixer` | MixerSessionDocument + routing invariants | no JUCE UI/WebView/driver dependencies unless proven migration-safe |
| `runtime/pcm` | existing prepared graph/publisher/callback | prepared immutable data + realtime-safe primitives |
| `adapters` | WebView transport, JUCE device, VST3 vendors, persistence, installer | narrow domain/application contracts |

Prefer direct CMake logical targets/interface libraries when migration benefits, with explicit PUBLIC/PRIVATE includes; split the 1600-line root CMake by targets only with byte-identical build outputs. Do not create per-feature shared copies of IDs, graph, device manager, or FX host.

## 6. Extension rules that enable hundreds of future features

- New **built-in FX**: descriptor identity/version/capabilities, isolated DSP, preparation, process, latency/tail, bypass, state migration, editor lifecycle, generic host dispatch, acceptance tests. No new per-FX switch in `Main.cpp`, no new audio output callback.
- New **routing/send/BUS**: typed intent and graph compiler with stable IDs, cycle safety, exact EdgeId gain/tap/ON, offline retention, installed readback and negative ACK. Browser UI cannot infer physical port admission.
- New **multi-device I/O**: independent capture streams feed one existing render-clock graph; per-device SPSC/drift handling; do not attach multiple unrelated DSP graphs or claim every vendor dual-ASIO configuration works. Coordinate with existing research PR #757.
- New **GUI feature**: new focused ViewModel from accepted readback, additive DOM within approved renderer, keyboard/accessibility/gesture tests, no alternate public preview.
- New **Player/Show/session**: virtual source through same graph and backward-compatible `.smix`; no autoplay, no unrequested output reroute.
- New **public distribution**: source SHA + compiled feature catalog + embedded assets + installer/portable parity; recent missing ImmerSIB FAT is the negative regression fixture.

## 7. Product build and reliability contract

Use one declared **distribution profile** (for example `public-alpha`, `field-fat`, `research`) feeding common CMake configure flags. The authoritative public/field compiled FX catalog and scene assets must be verified from the actual executable/installer, not only an environment variable or test count. Research profiles may explicitly exclude experimental/optional FX without masquerading as user FAT. Existing public alpha and D5 PR #760 are protected baselines; migration must not silently alter them.

For every impactful change capture: output device identity/epoch, callback deadline and xruns, p99/p99.9 block time under same hardware and buffer, plugin reported PDC, session restoration, graph revision/ACK, and GUI control-to-readback. Refactor acceptance is **no measurable degradation against recorded baseline** under equivalent conditions; never invent milliseconds or claim a physical ASIO result from CI. If data are missing, record `NOT MEASURED`.

## 8. Migration milestones (large deliverables, bounded risk)

| Stage | Deliverable | Required exit proof |
| --- | --- | --- |
| A0 governance & evidence | One authoritative PR template and live-source reconciliation protocol; consolidate stale four-lane guidance | Single-owner path; CI gate remains enforced; no DSP/GUI/source change |
| A1 vertical MVVM pilot | Extract one **routing/WIRE** ViewModel and typed intent/readback projection from existing approved adapter | Same GUI, full negative-ACK/offline/WIRE tests, no optimistic LIVE |
| A2 thin native host | Extract a cohesive **routing/session** use case from ConsoleWindow into existing native control owner | Direct cold start and `.smix` golden; no new authority/callback |
| A3 unified FX extension | Demonstrate one fixture FX / parameter added through generic registry/editor/lease with no new Main switch | Native PCM, VST3 reorder/bypass/state/editor stability |
| A4 measured M4 retirement | Remove one proven-obsolete legacy path per vertical PCM parity fixture | SOURCE ONLY, BUS FX, ChannelId physical output, device rebind and rollback |
| A5 architecture fitness + fast CI | Dependency/contract tests, compiled feature matrix, reduced change surface + stable merge integration | Failing structural case correctly blocks merge; docs-only stays cheap |
| A6 long-term governance | Short navigable CURRENT/historical docs, dependency owners, deliberate merge queue only if CI supports it | New agent finds exact owner/contract in one pass; zero blind old-stack merge |

The first working vertical pilot must ship tests and code together. Stage A0 is **research/governance only**; it does not satisfy A1–A6.

## 9. Stop-ship conditions

- More than one native DSP/PCM graph or UI renderer becomes authoritative.
- A refactor changes accepted SonKu/ImmerSIB sound, ChannelId/EdgeId identity, `.smix` compatibility or physical output permissions without explicit functional milestone and tests.
- A new UI layer claims installed/audible status from logical-only ACK.
- A callback calls UI, allocates, blocks, does file I/O, or destroys vendor plugin/editor.
- The field FAT installer omits a feature that is mandatory in the public user build.
- A merge uses stale branch/PR source or skipped/failed required CI.
- The developer reports “finished” for code/CI when hardware FAT, visuals, or release delivery are still pending.

**Success metric:** future independent features need fewer unrelated production modules changed; faster targeted tests, fewer merge conflicts and regressions, no performance regression. The metric is not line count or the number of classes created.

## 10. Research/source trail

See the linked authoritative Ardour, Tracktion, JUCE, Steinberg, Fowler, Cockburn, DORA, GitHub pages above. Additional CMake preset standards: https://cmake.org/cmake/help/book/mastering-cmake/cmake/Help/guide/user-interaction/ . These references inform design principles; they are not vendor endorsement of an exact ArSonMix code structure.
