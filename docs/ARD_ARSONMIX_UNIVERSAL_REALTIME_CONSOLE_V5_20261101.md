# ArSonMix ARD v5.1 — Universal Realtime Mixer Graph + Operator-First Console
Date: 2026-11-01 | Updated: 2026-11-01, mandatory puzzle-socket ABI amendment | Status: **BINDING ARCHITECTURE TARGET (not a claim of implemented online graph)** | Companion: [PRD v5](PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md). Supersedes workflow/implementation ordering in ARD v4.1 while preserving safety, signal-processing and accepted GUI baseline.

## A. Source-grounded audit and non-negotiable direction

**Currently shipped as of main cad535e67:** one JUCE primary audio callback renders fixed CH1–CH10 and four AUX via LiveDryRoutingLab/PreparedMixerBlock, physical MAIN/MONITOR and optional buffered secondary playback. Legacy occupied CH1 has its own VST lease/Orbit path (processLegacyChannelOne), although native patched empty CH1 and CH2–10 have routed FX through merged #261. Source and processing channel format are partly distinct in native v2 but web/app.js still has index-based display and snapshot restoration; web/monitor.js and GUI input drafts are fixed-ten. The offline DynamicChannelRoster / DynamicPreparedMixerBlock / publisher are preparatory components, **not** a production CH11 callback. #263 is a draft restricted dry-Group pilot. #265 is a draft GUI/workflow correction, not integrated production evidence. #15 real hardware 0FX crackle remains open. Do not override this truth with mock screenshots or obsolete PR ledger.

**Architecture decision:** one canonical stable-ID graph/processor, one native plugin lease owner, one accepted native route/snapshot/ACK authority, one primary JUCE mix callback and one UI projection. Build on existing engines; do not create a second DSP engine, second callback, WebAudio mix path, JS VST emulator or independent browser routing controller. Stage the full-fidelity legacy migration first; then add truly dynamic nodes and online graph edits.

## B. Canonical model (source != channel != bus != destination)

Stable 64-bit ChannelId and typed NodeRole:
- **InputChannel:** may have a PHYSICAL mono/stereo source, named software/loopback source, bus/submix source (where legal), or NONE; owns processing format MONO/STEREO, gain, VST3 inserts, sends, primary OUT, meter.
- **GroupBus:** independent PCM accumulator, no pretend physical input, independently mono/stereo, inserts, meter/fader/OUT; used for exclusive serial primary submix.
- **FxReturn:** independent PCM accumulator and owned wet plugin inserts, mono/stereo, independent fader/meters/OUT; parallel send graph retains source dry OUT.
- **MonitorBus/CueBus/MainBus/SubOutput:** output roles with explicit destinations and permission/cross-domain rules. MAIN and MONITOR are not two separately recomputed mixers. Cue/Listen is not an accidental Main feed.
- **VCA/Link:** *control-only*, never PCM buses; do not disguise a VCA as a summing Group.

A channel has ChannelId, ordinalPresentation, name/color/visible, ProcessingFormat, SourceBinding, PanMode, ordered InsertIds, SendEdges, PrimaryOutId, param targets and MeterId. Presentation index/name and physical pin ordinal are NOT identity. SourceBinding is NONE/PHYSICAL/LOGICAL/BUS with sourceFormat and verified L/R PortRefs. SendEdge has SendId, sourceId, sinkId, tap PRE/POST, level/pan, enabled, conversion and optional sidechain role. PCM DAG must be acyclic; physical speaker/output edges are separate from bus edges. Explicitly reject self or backward feedback, illegal output alias or duplicate render contributions.

**DevicePortRef** binds backend/endpoint identity, device epoch, verified enabled pin, callback ordinal map, capture/render direction and clock domain. LogicalInputId/OutputId may survive a saved Show; device replacement requires operator confirmation of new pin mapping. Never infer the right pin as left+1 when only a nonadjacent pair is verified.

**Stable IDs:** maintain maps of ChannelId→DSP node, InsertId→prepared lease, SendId→native edge; displayed bank position must never be used as a plugin ownership key. Runtime channel count is bounded by explicit memory/CPU/scratch/driver budget, not an arbitrary hardcoded CH10 or an unbounded allocation claim.

## C. Fixed signal order and mono/stereo conversion

Required default graph semantics for inputs:

~~~text
Verified capture SOURCE (M or L/R)
  → source decode / channel-format conversion / input gain & polarity
  → admitted channel HPF/trim & INSERT chain
  → PRE-FADER send tap (after declared channel processing)
  → channel fader + mute/solo + mono PAN or stereo BALANCE
  → POST-FADER/POST-PAN send tap [tap ordering documented per edge]
  → PRIMARY OUT to GROUP or MAIN, exactly once
  → destination bus inserts/fader/meters
  → MAIN processing/level/mono check → verified hardware and optional secondary tap
~~~

The accepted previous mixed path is the oracle during migration: if historical PRE/POST and polarity positions differ from the ideal order, write an explicit compatibility migration and compare golden PCM before changing audible behavior. Meter placement: channel LED meter = actual post-DSP/fader signal where currently specified; MONO is one data series, STEREO is two independent native L/R series. No MAX(L,R) duplicated as dual. Group and FxReturn meter after own processing. Peak clip latch is native-grounded; audio-rate data crosses to GUI through throttled telemetry.

Format conversion is explicit and performed **once** at a defined edge:
- PHYSICAL MONO→processing MONO: 1 stream, mono pan projected to stereo only when arriving at stereo destination.
- PHYSICAL MONO→processing STEREO: deliberate L/R projection, no imaginary second capture pin; plugin I/O semantics must be negotiated.
- PHYSICAL STEREO→processing STEREO: preserve original L/R, stereo BALANCE by default (no mono summing).
- PHYSICAL STEREO→processing MONO: default 0.5×(L+R) at ingress, allow explicit policy in future, retain L/R hardware bindings for undo/Show.
- MONO→STEREO bus and STEREO→MONO bus: apply declared conversion once, test headroom/phase. No accidental +6dB, double fold or double dry.
- Format flip changes **channel processing**, not source pins or primary OUT. A MONO source in STEREO channel displays two actual post-projection meters, possibly equal if centered, not fabricated L/R values.

Each VST3 instance must negotiate an actual compatible mono/stereo input/output arrangement off callback. A mono-only plugin on stereo channel may use a documented safe adaptor only if its signal semantics are verified; otherwise return INCOMPATIBLE_LAYOUT and leave prior graph untouched. Capture plugin latency samples for PDC computation; do not claim zero extra latency when the selected VST reports positive latency.

## D. Dual control planes (ONE audio authority)

### D1. Real-time parameter plane: NEVER force global MUTE

For admitted existing NodeIds/Edges: fader, pan/balance, mute, solo, enabled existing send gain, Aux/Group/FX Return gain and safe bypass automation use bounded atomic/latest-wins targets and short dezippered ramps in existing callback; no graph rebuild, device restart or plugin recreation. Validate finite values, ChannelId/SendId and accepted revision. Continuous motion may coalesce on the control/UI thread; only the latest bounded target is needed. Native control ACK/presentation is not UI-generated and must use stable IDs. Changes to send topology/tap/destination are structural, not the same as adjusting its gain. Hardware emergency mute is a separate independent high-priority action.

### D2. Structural graph plane: prepare off audio callback, then publish

Create/duplicate/delete channel; change channel width; add Group/FX Return; change source kind, primary destination, PRE/POST edge; reorder/install VST3; apply Show; output patch; plugin bus layout/PDC:

~~~text
UI gesture (single action) -> intent(requestId, baseRevision, deviceEpoch, stable Node IDs)
  -> control-thread validate IDs, permissions, device masks, edge DAG and resource budget
  -> prepare candidate graph, fixed scratch frames and plugin layouts/leases off RT
  -> calculate path latency/PDC and sample/frame budget
  -> COMMIT intent to existing immutable native publisher
  -> one atomic graph pointer/revision transition at an audio block boundary
  -> optional bounded de-click/crossfade for changed routes if budget admits
  -> ACK(acceptedEpoch, newRevision, changed IDs, route/lease truth)
  -> GUI replaces pending overlay with accepted snapshot
  -> old graph/plugins retired ONLY when all RT readers and plugin editors release
~~~

**Fail-safe invariants:** A stale epoch/revision, invalid source or output, VST prepare/layout/chunk failure, graph feedback, over-budget render or hotplug rejection leaves the *previous accepted graph and audible output* intact. If the actual hardware endpoint vanishes, fail silent and require deliberate reconnect/remap/ARM; never output to the newly available first hardware device by guess. Competing commands serialize by base revision, not by UI panel; no second simultaneous APPLY and no stuck pending after rejected/cancelled native ACK.

**Online graph edits:** Inserting FX/Group/FX Return on an already-running software graph must be prepared without a blanket operator MUTE **once** proven compatible and safe. No duplicate permanent renderer; if transition crossfade temporarily needs parallel PCM, allow only a bounded prepared overlap with explicit deadline/headroom preflight and retired-lease lifetime. Plugins that cannot be safely prepared for transition must produce a descriptive REQUIRES_DISARM rejection/confirm rather than covertly muting the stage. Changing backend, sample rate, clock domain, physical MAIN endpoints or unsafe output topology may still need guarded mute/disarm and explicit re-arm policy; internal automaton handles the steps after one operator confirmation, not four manual buttons. No auto ARM on hotplug.

### D3. Accepted-state / UI protocol

Use one native response envelope, not independent JS booleans:

~~~text
Intent {requestId, op, nodeIds[], baseGraphRevision, deviceEpoch, payload}
Progress {requestId, phase: PREPARING|PUBLISHING|AWAITING_ARM, optional explanation}
Accept {requestId, acceptedGraphRevision, deviceEpoch, changedNodeIds[], authoritativeSnapshot}
Reject {requestId, reasonCode, explanation, previousAcceptedRevision, recoverable}
~~~

JS holds **AcceptedSnapshot** and **one bounded PendingIntent overlay**; each view derives from this same projection. A pending source or mono/stereo choice may render an in-progress label, but cannot change accepted meter provenance or falsely mark an FX live. Explicit requestId filters stale ACK. On reject, restore canonical accepted view, preserve user focus and optionally a reversible uncommitted editor draft; release disabled controls. Recoverable no-hardware state offers one clear next step.

A single-gesture workflow may hide internal SAFE_MUTE/PREPARE/ACK/ARM, but the native engine—not JS—decides when output is safe to arm. Device inventory is separate from ordinary audio graph changes. Never relabel a software UI draft as a physically verified route.

## E. VST3 lifetime, plugin routing and ownership

One **NativeFxInstanceRegistry** keyed by (ChannelId, InsertId, generation) owns each actual JUCE AudioPluginInstance, class UID, catalog metadata, vendor state/chunk, editor leases, bypass, latency and negotiated bus configuration. Registry creation, scan, state save/restore, prepare, bus negotiation, and disposal are **control-thread** tasks; audio callback only reads prepared processor references. The exact same API serves CH1, CH2, CH11, GROUP, FX RETURN and MAIN when admitted. No special CH1 owned DSP in final graph; migrate CH1 Orbit and editor first with identity/state tests rather than deleting or cloning them. Silent/unpatched strip can own an inactive prepared VST3 rack with no audio yet; source binding does not gate FX creation.

For insert reorder, a prepared routing update retains existing instance identity and no unnecessary reinstantiate. Removed instances and their editors retire safely after readers quiesce. Never free VST while an editor or audio block holds it. Plugins can block or crash independently; admission failures and unsupported layouts must fail closed; do not promise in-process isolation from arbitrary vendor plugin crashes. Plugin delay compensation is graph-aware and bounded by configured maximum added latency; preserve shortest safe live monitor path and show actual delay/bypass policy to operator.

FX RETURN is an independent processing bus with send fan-in; insert VST3 via click/drop to SEND may create return+send in **one atomic transaction**. Group Selected changes each selected source's primaryOut (and GROUP→MAIN) atomically, retaining their unrelated parallel sends. Do not double-feed MAIN. Include PRE/POST and plugin latency interactions in the same compiled DAG.

## F. GUI architecture and rendering contract

WebView2 is a lightweight control/presentation layer, **not PCM authority**. Preserve approved compact L2/RAIL3 geometry and tactile fader assets, 7-row aligned strips, small typography, no acrylic blur/glow or filter-heavy hover on entry-level iGPU. Do NOT clone a full browser DAW shell.

- Native roster is the only source of channel count/type/order. Render accepted ChannelIds in a horizontally virtualized or windowed bank; left/right bank controls jump **visible strip count** while MAIN remains pinned. Selected ID and FX editor focus do not change when bank changes. A "+ CHANNEL" skeleton can be enabled only after graph capabilities admit real dynamic create.
- Reusable ChannelStrip presentation takes accepted ProcessingFormat. M = exactly one native peak; ST = exactly two L/R independent peaks. Stereo LEDs keep fixed thresholds; bars light within fixed geometry rather than a deforming red-orange-green gradient. Any meter view uses a bounded telemetry sample and dirty update; avoid repeated DOM query, style/layout thrash and per-peak entire-strip render. Clip/hold semantics preserve native evidence.
- One UX command model for IN, M/ST, INSERT, SEND, OUT. Use existing controller surfaces web/app.js, web/mixer-io.js, web/monitor.js, web/bridge.js as migration seams, **consolidate** split ownership rather than adding another global. Move browser-local session storage away from native truth: durable Show comes from native data, UI drafts only decorate.
- FX Rack must be functional on every silent/patched accepted channel with prepared VST3, not a CH1 fallback or permanently disabled staged rows. Advanced PATCH matrix is for expert batch work, not a required one-channel source gesture. User-created GROUP/FX bus has distinct chip/role color, source count and legal OUT list.
- Responsive at 1366×768, 1440 and 1680 desktop; input/FX menus do not obscure faders, popover inspectors never push the mixer grid. Pointer/drag should remain responsive on a standard Windows GPU; benchmark steady and when channels+meters scale. Keyboard focus/ARIA for each bank/strip/operation.
- Keep stand-alone/offline preview in parity with source-owned WebView HTML and CSS generator; browser simulation must be clearly labeled UI PREVIEW and never shown as native audio proof.

## G. Exact native cutover plan — meaningful vertical milestones only

**M-U1 — Full-fidelity one-callback production cutover (engine-first).**
1. Capture deterministic golden rendered PCM from 10CH+4AUX with active legacy CH1 plugin/Orbit, CH2+ routed multi-VST, independent MONITOR MAIN output pins, optional secondary playback tap, PRE/POST, fader/pan/mute/solo, channel mono/stereo, saved Show and invalid-ACK tests.
2. Extend existing DynamicChannelRoster/PreparedMixerBlock and DynamicNativeSessionPublisher to retain exact FX lease identity, buffer scratch, plugin layout, physical output policy and scenes. Build off RT.
3. In LiveAudioEngine callback, select old OR new renderer for an admitted atomic session cutover, never mix both; remove processLegacyChannelOne only after its actual editor/Orbit vendor tests pass. Preserve old user sessions via versioned migration.
4. Native regression: PCM A/B within declared floating tolerance, side-by-side L/R meter provenance, no duplicated MAIN/monitor return, plugin state/instance identity and rollback after bad graph. Windows native build + CTest required. Do NOT merge limited #263 dry-Group pilot as a substitute.

**M-U2 — Real operator Console vertical slice.**
Create CH11 mono/stereo and FX on an initially silent channel. On same authoritative graph, select CH1+CH2, add new independent GROUP, attach preexisting VST3 and independent FX RETURN from SEND, maintain MAIN/MONITOR and Show. Only now activate + CHANNEL skeleton, bank virtualization and one-click OUT/SEND. One driver/source/FX/editor set of APIs for all ChannelIds.

**M-U3 — Live continuity and advanced completeness.**
Prove safe online graph handoff for add/reorder VST, Group/FX send, and format changes under bounded device/CPU headroom. Implement graph-aware plugin latency/PDC, MASTER FX, cue/PFL/AFL, automation/scene recall scope, control-only VCA and (separately gated) virtual secondary audio timing/ASRC. Ordinary gains/parameters remain no-mute, no-recompile. No invented compatibility with proprietary DAW Mix Engine FX.

**Release gate in parallel:** #15 real ASIO and WASAPI zero-FX hardware crackle: 30-minute diagnostic, actual source→fader/mute→MAIN acoustic continuity (no direct monitor bypass), then 1FX/multiFX. Distinguish capture discontinuities, render starvation and secondary clock underrun; never present capture counters as proof no speaker crackle. Actual hardware result required for Stable label.

**CI efficiency:** one production native graph owner, one FX lease owner, one control UI owner, one Show owner. Work from exact main; test targeted C++ pure engine and JS first, then a single exact-head Windows MSVC/CTest/package plus Chromium layout/interaction run. No repeated heavy CI for prose-only changes. Do not merge overlapping branch heads blindly. #265 UI draft and #263 dry pilot require explicit reconcile against this cutover and latest main.

## H. Mandatory acceptance matrix (all observable)

| Test | Native/golden evidence | GUI evidence | Failure and recovery |
|---|---|---|---|
| A1: CH1/2/7/11 M↔ST | PCM conversion, source format distinct, true L/R measured | one/two meter LEDs, no fader geometry jump | incompatible plugin layout rejects with old sound |
| A2: CH11 addition, unpatched FX | stable new ChannelId/InsertId and real plugin lease before input | +CHANNEL and editable FX | CPU admission reject, no fake strip |
| A3: Group Selected with VST/AUX/MONITOR/Show | only selected primary paths redirected; no double-dry; plugin instances retained | grouped role/output and sends | feedback/cycle/stale revision rollback |
| A4: FX Return | parallel send + wet bus + independent output, correct PRE/POST/tails | click/drop SEND, visible FX meter | plugin missing or bus incompatible preserves dry |
| A5: Mono/stereo real pins | nonadjacent L/R, mono source projected and 0.5 L+R fold once | verified IN picker, truthful format | phantom pin/epoch mismatch rejected |
| A6: Live parameter movement | no graph compile/restart/OS lock, smoothed gain/mute | responsive fader/send, real native ACK | lost ACK/retry doesn't wedge controls |
| A7: Output and dual clocks | no duplicate plugin DSP, monitored MAIN/MON and optional ASRC consumer | legal routing names only | hotplug disarms; no autoarm/wrong speaker |
| A8: FX/Show | vendor chunk, UID, editor/instance lease round-trip, PDC metadata | restored channel name/format/FX/sends | corrupt chunk/driver rebind rejects atomically |
| A9: Long-running physical FAT | ASIO and WASAPI #15 0FX, then 1/multiFX with captured diagnosis | SIGNAL CHECK raw vs postFX vs MAIN | audible crackle blocks Stable |
| A10: GPU and UI parity | no JS audio ownership; no per-frame heavy DOM rebuild | 1366/1440/1680 iGPU, bank arrows/pinned MAIN, accessible | hidden/banked strip keeps live DSP |

## I. Existing file seams, boundaries and references

- Native production: src/LiveAudioEngine.h, src/LiveDryRoutingLab.h, src/Main.cpp.
- Typed graph/compiler: engine/core/DynamicChannelRoster.h, DynamicPreparedMixerBlock.h, DynamicNativeSessionPublisher.h, LiveChannelFxGraph.h, PreparedMixerBlock.h, NativeDryRouteCommand.h; migrate, not duplicate.
- Native Show and FX leases: src/NativeShowSchema.h and current native plugin registry/editor code; replace CH1-specific ownership incrementally.
- GUI: web/app.js, web/mixer-io.js, web/monitor.js, web/bridge.js, web/mixer-layout.js; source-owned web/styles-src, scripts/build_ui_css.py, offline standalone.
- Engineering baseline: [ADR R5](ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md), [PRD v5](PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md), [current code ledger](RECONCILED_PROGRESS_ARSONMIX_20261101.md), [issue #192](https://github.com/masarray/arsonmix/issues/192), [hardware #15](https://github.com/masarray/arsonmix/issues/15).
- Functional evidence (not internal algorithms): https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm ; https://fenderstudiopromanual.fender.com/en/Content/Recording_Topics/Audio_Tracks.htm ; https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html ; https://helpx.adobe.com/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html .

## J. MANDATORY V5.1 ABI AND MODULE-BOUNDARY AMENDMENT — PUZZLE SOCKET V8

**Effective 2026-11-01; precedence:** [Puzzle Socket v8](PUZZLE_INTEGRATION_CONTRACT_V8_20261101.md) is the active parallel integration sequence, with normative required-field inventory at [contracts/console-puzzle-v8.json](../contracts/console-puzzle-v8.json) and [PRD v5.1](PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md) as the product acceptance. All earlier RT safety invariants in this ARD remain binding. **JSON schemaVersion stays 6**; "v8" names the orchestration contract, not a new wire protocol version.

### J1. Three implementations, one shared stable socket
- **A / ENGINE #266** owns \`src/**\`, \`engine/core/**\`, native JUCE callback and plugin lease/Show tests. It is the **only** source of actual device pin masks, accepted revision, ChannelId, plugin state and measured PCM. It must serialize real \`ConsoleSnapshot\`/ACK through the existing WebView bridge. When the current callback is M4, \`productionCallbackLive=false\`, \`supportedOps=[]\` and mutations reject; that is SAFE scaffold, not delivered functionality.
- **B / GUI #271** owns \`web/**\`, UI/Playwright tests and deterministic generated standalone parity. It renders accepted native state and emits intents but never compiles or simulates the authoritative DSP graph. It can work against an ENGINE-produced unready fixture in parallel; LIVE requires the actual installed native callback and fresh matching ACK/snapshot.
- **C / INTEGRATION #270** owns canonical fixtures, contract validators, cross-layer Windows/Chromium CI composition, package provenance and internal QA. It must consume the **actual native producer output**; fabricated hand-authored PCM and synthetic G2 fixtures may validate schema behavior but NEVER qualify production audio.

Native control thread owns \`requestId\` replay protection, stable IDs, safe immutable graph prepares and publication. RT callback has no allocation, lock, disk, driver scan, GUI event handling or plugin instantiation. One PRIMARY JUCE render callback; do not overlay second native/JS renderer. \`ChannelId\` is not physical pin index, UI strip ordinal or channel caption.

### J2. The exact ABI to test across all modules
Events: \`arMixConsoleSubscribe\`, \`arMixConsoleSnapshot\`, \`arMixConsoleIntent\`, \`arMixConsoleAck\`.

- Intent: \`{requestId,deviceEpoch,baseGraphRevision,baseRosterRevision,operation,targetChannelId,payload}\`.
- ACK: \`{requestId,status,deviceEpoch,graphRevision,rosterRevision,errorCode}\`, status \`ACCEPTED|REJECTED|REQUIRES_DEVICE_CHANGE\`; echo exact opaque requestId. ACK is not sufficient for LIVE: await a fresh full accepted snapshot with matching device epoch and graph+roster revision.
- Snapshot: \`{schemaVersion:6,productionCallbackLive,device:{epoch,backend,captureDevice,playbackDevice,inputEndpoints,outputEndpoints,enabledInputs,enabledOutputs,sampleRate,bufferFrames,outputArmed},graph:{revision,rosterRevision,channels,destinations,capabilities:{callbackOwnsChannelIds,supportedOps,dynamicChannels,groups,fxReturns,monitor}},telemetry:{rawCapturePresent,rawCapturePeak,mainPcmPresent,monitorPcmPresent}}\`. Optional extra fields must be documented without changing required-field meanings.
- Native 64-bit identity / device epoch / graph and roster revisions serialize as canonical decimal **strings**; GUI uses BigInt comparison, never implicit JavaScript Number loss. \`requestId\` is an opaque bounded printable \`session-unique:sequence\` token, not a simple counter reused after WebView reconnect. Duplicate, stale-epoch/revision, invalid pin, missing role, cycle and unsupported-operation rejection must preserve the accepted graph and PCM.
- InputEndpoint \`port\` is a verified active backend device pin and \`callback\` is its present JUCE callback ordinal; disabled/missing pins are absent or explicitly unavailable. SOURCE mono/stereo and DSP processing M/ST are independent; physical stereo requires two **distinct enabled** capture pins. Output endpoints must be checked against actual active playback pins.
- \`productionCallbackLive=true\` is set by the authoritative installed callback only after full-fidelity CH1 vendor/Orbit/VST3, CH2–10+4 AUX, FX/Show/MAIN/MONITOR ownership and output admission; individual \`supportedOps\` reflect **real** working operations. No offline class, JS demo or route ACK can turn these on.

### J3. Machine-testable vertical integration rather than parallel mock success
**S0 interface convergence:** ENGINE emits \`SNAPSHOT_UNREADY\` + \`ACK_REJECTED\` directly from C++ producer; GUI renders/replays exact fixtures without fake LIVE; FAT rejects missing required fields and different fixture hashes. The gate fails if actual native \`enabledInputs/Outputs\` are omitted while FAT requires them. If an adapter translates schema, it must be versioned and covered by both producer/consumer tests; do not allow two "canonical" shapes.

**S1 first native path:** one JUCE production callback executes verified capture IN1→CH07→MAIN, real fader and mute/route ACK, RAW/CH/MAIN telemetry and user-facing one-gesture Console. Keep M4 CH1 occupied vendor/Orbit/VST ownership and Show parity before replacing renderer. One pinned MAIN hardware destination with explicit output-arm. Regression: one-pin source cannot be represented as stereo IN1/IN1; no input-fed signal means no RAW PCM even when route is ACKed.

**S2 full console path:** distinct IN1→CH07 and IN2→CH02, genuine independent VST3 leases/editor/bypass/reorder, dynamic CH11, Group Selected serial exclusive primary output, FX Return parallel PRE/POST wet, independent MONITOR output, Sends-on-Faders and Show vendor-chunk roundtrip. Golden PCM + UI events + full snapshots must all refer to the SAME accepted stable ChannelIds and revision; no double dry and no FX tail clipping on safe edit.

**S3 release gate:** single exact integrated SHA tests Windows MSVC+CTest/real-callback PCM, Chromium/Playwright, bundled CSS/HTML deterministic parity, invalid edits/rollback, software VST safety, low-end GPU responsiveness, hashed EXE/provenance. Hardware rig confirms ASIO/WASAPI/physical crackle #15 when available; otherwise mark physical FAT **PENDING**, not passed. No G1/G2 partial customer testing; one finished product UAT after S3.

### J4. Hard coordination rules and mechanical checks
Canonical machine inventory lives in \`contracts/console-puzzle-v8.json\`. Executable seam validator from PR #279 (once merged) is \`node scripts/check_console_puzzle.mjs\` for cheap G0 tests and \`node scripts/check_console_puzzle.mjs --release\` for fail-closed integrated-source preflight. A PASS proves **only schema/source compatibility**, not audible sound; require Windows native/Playwright/physical QA separately. Never weaken tests to hide missing native operations.

No independent PR may redefine a field, operation enum, status or channel ID format. Changes to socket need an issue #192 diff, updated ENGINE-produced fixture, GUI consumer and FAT checker in one integration checkpoint. File owners do not write another lane's business logic. Incompatible changes are merged into a controlled integration branch and rejected until all three puzzle pieces pass the same fixture. Avoid CI waste: targeted tests before push, full workflow only for integrated slices. User is not the debugger for incomplete builds.
