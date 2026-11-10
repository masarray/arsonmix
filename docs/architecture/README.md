# ArSonMix — Current Architecture & Source-of-Truth Map

Status: CURRENT ORIENTATION (audit 2026-11-11). This document describes verified repository source and the proposed ownership model; it does NOT certify the installed Windows application or physical audio. Refresh current main/PR HEAD before changing code.

**Feature implementation:** [Hotkey Register + FL Studio External OUT execution contract](FEATURE_HOTKEY_FL_OUTPUT_EXECUTION_20261111.md) · [Hotkey/output next-thread prompt](HOTKEY_OUTPUT_NEXT_THREAD_PROMPT_20261111.md). These are research-backed implementation workpacks; neither feature is shipped until native/audio/FAT evidence proves it.

**Entry points:** [Execution blueprint](MAINTAINABILITY_EXECUTION_V1_20261111.md) · [Fast feature playbook](FEATURE_EXTENSION_PLAYBOOK.md) · [Next-thread prompt](NEXT_THREAD_EXECUTION_PROMPT.md) · [Issue #527](https://github.com/masarray/arsonmix/issues/527).

## What this application is

ArSonMix is a native JUCE live digital mixer and VST3 host with one approved assembled /preview/ GUI rendered by WebView2; browser preview is visual/simulated, not a browser audio engine. Native hardware audio authority is the ONE JUCE AudioIODeviceCallback and its installed ChannelId/EdgeId PCM graph, with M4 compatibility only where explicitly still needed. A logical/offline MixerSessionDocument can be edited without hardware but is NOT permission to claim audible PCM.

The current path to reason about every feature is:

~~~
Assembled /preview/ DOM (presentation only)
  -> web/approved-native-bridge.js + web/approved-native-contract.js
  -> Native JUCE command transport in src/Main.cpp (MESSAGE/CONTROL thread)
  -> src/NativeLogicalMixerControl.h for unbound edits and readback
  -> accepted logical MixerSessionDocument / ChannelId + EdgeId model
  -> validated mixer graph & FX lease preparation (CONTROL, not callback)
  -> engine/core/DynamicNativeSessionPublisher.h + realtime publication
  -> ONE src/LiveAudioEngine.h JUCE callback (with src/LiveDryRoutingLab.h compatibility integration)
  -> verified ASIO/WASAPI device outputs, optional independent stream
  -> native status/ACK then accepted revision readback -> exact same GUI
~~~

*Logical ACK/readback* means a durable native logical edit was committed and subsequently read back. *Installed PCM ACK/readback* means an actual prepared graph is installed and bound to genuine device/pin identity. They must never be conflated. A third state, *audible*, requires valid authorized output routing; a browser 'ready' event or successful CI cannot prove it.

## Current file ownership (grounded in current main source)

| Concern | Current primary files | Correct responsibility / future direction |
| --- | --- | --- |
| Application composition / bridge transport | src/Main.cpp; src/ApprovedConsoleNativeTransportV6.h | JUCE app lifecycle, WebView wiring, dispatch only; move domain-heavy control use-cases behind narrow services incrementally |
| Logical mixer & accepted session | src/NativeLogicalMixerControl.h; src/NativeLogicalMixerWorkspace.h; src/NativeMixerSessionDocument.h; engine/core/DeviceIndependentMixerSession.h; engine/core/DynamicChannelRoster.h | One accepted workspace. The control service validates requests, stages edits, commits through the shared save worker and returns correlated ACKs. The workspace owns stable IDs and logical readback. |
| Session persistence | src/NativeMixerSessionStore.h; src/NativeMixerSessionSaveWorker.h; src/NativeMixerSessionFxRestore.h | .smix durable state, rollback, vendor-state retention and explicit schema migration |
| Native graph publisher & PCM | engine/core/DynamicNativeSessionPublisher.h; engine/core/RealtimeRoutePublisher.h; engine/core/DynamicPreparedMixerBlock.h | Validated immutable prepared graph, atomic publication, safe retirement, callback render state |
| Audio device policy | src/LiveAudioEngine.h; engine/device/**; src/Main.cpp currently applies device | Verified driver/pins, epochs, reopen/rebind, explicit physical output safety; independent from logical session document |
| Audio device apply preflight (M2a) | src/NativeDeviceApplyCoordinator.h; src/Main.cpp (remaining orchestration) | Single message-thread transaction guard, JUCE open boundary, typed request/pin checks, accepted readback and fail-closed rollback. Next: extract full apply/rebind orchestration without duplicating DeviceManager |
| Production audio compatibility | src/LiveDryRoutingLab.h; engine/core/UnifiedLegacyProductionAdmission.h | Retain only proven required M4/FAT migration semantics; never create another render authority |
| Built-in DSP and VST lifecycle | engine/builtin_fx/**; src/NativeBuiltInFx.h; src/FactoryFxRegistry.h; src/PluginCatalog.h; engine/core/DynamicChannelFxLeaseBank.h; src/LiveAudioEngine.h | Per-processor DSP; one built-in registration for live insert, offline prepare, restore and native picker. Specialized editors and vendor lease remain with their existing owners. |
| Player | engine/player/**; src/Main.cpp dispatch | Worker decode / buffered PCM, one native graph virtual input, no independent hardware output callback |
| Native status projection | integration/ApprovedNativeRuntimeSnapshot.h; integration/ApprovedNativeV8Projection.h; src/NativeLogicalMixerControl.h | Canonical installed and retained projection, with a forwarding include for old clients. Logical-only readback is separate and cannot admit PCM. |
| GUI state, WIRE, device setup | web/approved-native-bridge.js; web/approved-native-contract.js; web/monitor.js; web/mixer-io.js; site/preview-v2/ (internal build input) | Render accepted native states only; one assembled/public/native /preview/; no competing DOM model or JS PCM |
| UI build & publishing | scripts/build_pages.py; scripts/build_approved_native_gui.py; site/preview-buttons/ (approved PNG overlay source) | Deterministic single assembled renderer and native bundle provenance, not a second public GUI |
| Tests & CI | tests/**; CMakeLists.txt; .github/workflows/{ui-smoke,p1-windows,chat-ci-milestone}.yml | Cheap relevant checks every PR update, deliberate exact-SHA UI or native/FAT milestone; physical FAT separate |

## M1 control events

- `arMixLogicalIntent` enters the logical control service. It validates the request ID, workspace token and revision, then commits the candidate document and referenced FX state through `NativeMixerSessionSaveWorker`. An `arMixLogicalAck` reports that commit's revision. A later `arMixConsoleSnapshot.logicalMixer` provides independent readback with `LOGICAL_ONLY` authority.
- `arMixConsoleIntent` retains the admitted native control path. The approved bridge waits for `arMixConsoleAck` and matching accepted readback before committing the visible control state. Retained graph readback carries no installed callback authority. A verified socket connection or retarget uses the existing renderer's gravity and impact animation after the accepted graph is drawn.
- `arMixCatalog` carries registered built-in picker metadata alongside verified external catalog entries. One registration in `NativeBuiltInFx.h` supplies the control-side prepare function and limits for a generic built-in insert. An effect with its own editor supplies that editor separately.

## Verified audit hotspots at main 99f580d76bf20ea994d54f8916098c680897f41d

- src/Main.cpp: 6,499 lines. Before M1, ConsoleWindow owned logical edits, native ACK/status, device selection/apply, built-in FX handlers, plugin editors, Player and UI responsibilities.
- src/LiveAudioEngine.h: 3,564 lines. The single callback shares a class with device lifecycle, legacy routing control, graph/plugin lifecycle, and monitoring policy.
- src/LiveDryRoutingLab.h: 3,261 lines. Production ChannelId paths coexist with legacy M4/Lab names/guards.
- web/approved-native-bridge.js: 3,646 lines. Many per-domain event/ACK/DOM pathways in one adapter.
- web/monitor.js: 2,118 lines; do NOT assume it is entirely in the currently assembled native GUI without checking the build.
- Before M1, two ApprovedNativeV8Projection.h headers defined overlapping projections. The canonical integration header now serves both callers, and the src header forwards to it.
- tests/ contains large routing, device, source/session and FX test suites. This is a major asset. Some source-text contract tests intentionally assert exact code strings; refactoring may legitimately need to update these *only when behavior-equivalent tests also exist*.
- docs/ contains 238 direct entries. The 2026-11-07 Device-Independent ADR says "NOT YET IMPLEMENTED", while merged PR #513 implements *logical-only Milestone A*. This is partially stale wording, not permission to claim hardware binding works.
- web/styles.css is dominated in bytes by an embedded base64 font; file size is NOT a maintainability score by itself.

## Truthful current limitations

README v0.5.0-alpha.10 identifies Windows as an alpha field-test build. Full physical ASIO, real singer monitoring, vendor VST3 continuity, device-loss behavior and uninterrupted audio require independent FAT. [Issue #490](https://github.com/masarray/arsonmix/issues/490), [#412](https://github.com/masarray/arsonmix/issues/412), [#192](https://github.com/masarray/arsonmix/issues/192), and [#15](https://github.com/masarray/arsonmix/issues/15) remain product/field-quality owners. A code restructure must not claim these resolved.

## Existing safety invariants (non-negotiable)

1. Exactly one approved /preview/ GUI, assembled deterministically, shared between public preview and native host. Do not use /preview-v2/ as a separate product.
2. Exactly ONE JUCE realtime callback, one installed ChannelId PCM authority; JS never handles or fabricates PCM.
3. Stable native ChannelId/EdgeId and revision/epoch fences; stale or rejected commands never change accepted audio state. Never portray LOGICAL_ONLY or RETAINED as installed or audible.
4. Processors, VST3 scans, editor lifecycle, session/JSON/disk, and graph construction stay off realtime callback. No RT blocking, allocation, logs or locks.
5. ASIO mic monitoring remains independent of non-real-time playback/secondary WASAPI stream. Actual device and output-pin identity must be verified; never auto-reroute output or defeat user mute/intent.
6. Device changes preserve mixer document, FX state and WIRE; intentional SOURCE ONLY and MAIN mute are respected; no extra monitor/ARM wizard to recover trusted already-approved output.
7. Do not use docs-only work, mock GUI results or CI green as proof of physical audio FAT.

## Reading order for a new coding agent

1. Root [AGENTS.md](../../AGENTS.md), [Smart CI runbook](../SMART_CI_RUNNER_WORKFLOW.md), current main + active PRs.
2. This map, [execution blueprint](MAINTAINABILITY_EXECUTION_V1_20261111.md), [feature playbook](FEATURE_EXTENSION_PLAYBOOK.md).
3. Only files and existing tests relevant to the chosen execution milestone; do not spend time reciting historical documents.
4. Implement and validate a cohesive code milestone, record the exact SHA and follow the bounded CI policy.

**Authority order:** current user instruction + root AGENTS.md/approved GUI and native safety contract > current tested code and merged source > this orientation/implementation plan > older dated workstreams. This map is a guide to engineering and can be updated after verified source changes.
