# ArSonMix — ARD v2.0: Unified Mixer, Native Hardware I/O, FX Rack and AUX/Bus Routing

**2026-10-30 native-connected hardware matrix implementation:** [implementation ARD](ARD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), [operator PRD](PRD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), [handoff](HANDOFF_HARDWARE_IO_MATRIX_20261030.md). Matrix crosspoints use existing `monitor.js` staging and the native ASIO/WASAPI route controller. UI-only incomplete stereo cannot publish audio; MAIN physical output requires ACK. Still fixed 10 live sources+4 AUX; MS3B MONITOR and DYN2 expansion are separate; #15 hardware FAT is open.

**ADR FXF1 architecture amendment (2026-10-30):** [Factory VST3 bundle ADR](ADR_FACTORY_FX_BUNDLE_20261030.md) + [manifest schema](factoryfx-release-manifest.schema.json) and [six plugin PRD](PRD_FACTORY_FX_20261030.md), epic #185. FactoryFX/ contains only hash/identity/licence verified FIRST-PARTY VST3 built in independent sibling repos; Studio/Plugins/VST3 remains USER-owned and unshipped in public package. Reuse one PluginCatalog/LiveChannelFxGraph lifecycle and validated graph PDC/Show, no JUCE8/9 source linking, no RT scanning/hashing, no fake FX or blanket rescan. Crossrepo ASKPVST #60, ArSonEQ #6, ArSonVerb #4. Merged G5A #178 eight-slot graph foundation != full GUI multi-insert, and MS1 #184 mono/stereo native != general VST bus layout negotiation. Existing 10CH/4AUX/ACK and L2 #176 remain. #15 hardware quality gate separate.


**R6 engineering correction / precedence (2026-10-30):** [current code audit and implementable native slices](R6_CURRENT_STATE_ARCHITECTURE_EXECUTION_20261030.md) and [professional GUI operator contract](R6_OPERATOR_WORKFLOW_CONTRACT_20261030.md) override earlier statements in this file that still refer to G1/G2 as future work. Latest audited main@8251bd6e already includes L2 #171 and G3 #168; protect immutable [L2 #176](https://github.com/masarray/arsonmix/issues/176), no master fader shrink or top toolbar. Source/channel MONO/STEREO coupling, stereo-only VST adapter, non-format-aware sends and only MAIN HW0/1 remain open; don't mark future typed groups, master FX, hardware monitor outs or full Show LIVE. Preserve one native graph, plugin ownership and revisioned epoch ACK. Implementation issues #173/#174/#175 then #95; #15 hardware zero-VST release gate runs parallel.


**ADR R5 issue map:** [#173](https://github.com/masarray/arsonmix/issues/173) source/channel native format; [#174](https://github.com/masarray/arsonmix/issues/174) typed bus/send conversions; [#175](https://github.com/masarray/arsonmix/issues/175) real physical output patch; #95 FX/PDC, #15 hardware FAT. #143 was CLOSED after earlier patch UX and must not be treated as output plan completion.


**Date:** 2026-10-30 | **Status:** CANONICAL TARGET ARCHITECTURE — implementation is milestone-gated, not automatically delivered.  
**Audited source baseline:** `main@3b89c86` (verify HEAD before every edit; source moves in parallel threads).  
**Read together with:** [Product PRD](PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md), [Execution Handoff](EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md), [FX/Routing cross-thread guard](FX_RACK_ROUTING_CROSS_THREAD_CONTRACT_20261030.md), [Smart Audio Engine ARD](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), [P0 Hardware FAT](HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md). This architecture supersedes the *navigation/routing roadmap* in the old v0.3 ARD, not its reusable tests or safety constraints.

## 0A. Evidence-to-graph and GUI design traceability
Reference specification: [GUI_WORKFLOW_BENCHMARK_20261030.md](GUI_WORKFLOW_BENCHMARK_20261030.md) and [CUSTOMER_VOICE_REQUIREMENTS_20261030.md](CUSTOMER_VOICE_REQUIREMENTS_20261030.md). Translate LV1/Studio One/FL interaction patterns into one authoritative model, not an independent JS audio renderer: physical device/pins, logical mono/stereo sources, MixerChannel, typed FX AUX/Group/Monitor/Matrix/Main buses, control-only DCA, per-route send taps, physical output patches and transaction status. These are future typed expansions where current code lacks them; present 10CH/4AUX/CH1-only multi-insert/MAIN L-R read-only truthfully. Preview UI must never masquerade as native LIVE.

When output buses are developed, first implement a dedicated compiled physical output ownership/multiout plan, device mask and clock/layout validation, control-thread memory preparation and callback-epoch publication; *then* wire GUI OUTPUT Patch. Named MON/SUB/REC destinations are distinct logical buses and may be unavailable on 2-output hardware. Do not mix output crossfeed or accidentally sum MAIN twice. PRE is post-insert/pre-fader, POST is post-fader; preserve explicit mute/solo and PDC policy. Channel DCA control is not a new PCM summing node. Graph geometry and UI navigation cannot change hardware, FX processing or routing unless a specific accepted transaction does so. Screenshot references remain design evidence, not redistributed assets.

## ADR-R5 addendum: independent source/channel/bus MONO/STEREO
The binding design decision and implementation gate are [ADR R5](ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md) and [R5 execution handoff](EXECUTION_HANDOFF_MONO_STEREO_REFORM_20261030.md). CURRENT code ChannelRoutingContract already supports ChannelFormat/foldStereoToMono, but NativeDryRouteCommand couples legacy mode to source/effective channel format and coerces BUS to STEREO; ChannelSendPlan and AuxBusPlan apply stereo balance coefficients, not mono-source pan; routed VST adapter is stereo-oriented; fixed native 10CH/4 stereo AUX/32 sends and MAIN L/R still apply. Target separated SourceBinding / processing ChannelFormat / typed BusFormat / RouteEdge conversion / HardwareOutputPatch with v2 codec and legacy migration. Reject unsupported plugin layouts and missing pins rather than faking right inputs. Preserve mono equal-power pan vs stereo balance and 0.5(L+R) fold, explicit policies and PDC. MS1 source/channel native+GUI flip; MS2 sends/bus format; MS3 physical output via existing #143; MS4 unified multi-FX and Show. All updates transactional MUTE->APPLY->ACK, no second realtime graph and no allocations/locks in callback. Current G3 PRE/POST SENDS editing is merged, not evidence that bus/channel mono/stereo reform is complete.

## 0. Decision record: one audio graph, several views
ArSonMix must behave like a usable **digital live mixer**. The user supplied nine concrete reference screenshots: eMotion LV1 fader/mode bank, comprehensive channel view, insert-rack/faders, Studio One mono/stereo device I/O matrices, DAW send-to-bus layouts and FL Studio's focused source→destination routing indication. The design lesson is **not** to copy those graphics. It is to keep a stable MIX surface while letting an operator inspect **FX / ROUTE / SENDS** without losing VST3 controls or silently changing the audio topology.

**ADR-R4-001:** `MIX`, `FX RACK`, `ROUTING`, `CHANNEL DETAIL`, `SHOW`, `SETUP` are presentations of **one** native session, one native plugin catalog, one physical device registry, one validated route graph and one control model. UI-local drafts are never the audio source of truth. Navigation does not construct, destroy, detach, move or reset an audio processing graph.

**ADR-R4-002:** Preserve the existing brand, compact metal console, `.btn-180` command buttons, channel identity colors, meter design, fader assets and low-end GPU budget. Copy workflows and spatial relationships, not eMotion/Studio One graphics, trademarks or code. Patch matrix cells are flat semantic state indicators, not raised `btn-180` keys.

**ADR-R4-003:** Remove any *presenter* logic that replaces the 6 visible insert rows on a physical channel with a `+ ROUTE / FX` control. Such a control may exist only as a dedicated routing command; never substitute it for the working FX rack. The CH1 original multi-VST chain executes via `LiveAudioEngine::processLegacyChannelOne` *inside* the native routed callback; UI must preserve its native `addCatalog / fxMove / fxRemove / fxBypass / fxEditor / Orbit` actions and stable physical instance identity.

**ADR-R4-004:** The existing 10 input channels + **4 dedicated stereo AUX FX returns** is the *minimum current DSP base*. AUX1..AUX4 are **not** four hardware-output jacks or four freely programmable Groups/Matrix buses. True physical output allocation, external monitor/CUE and multichannel matrices require new device-bound graph contracts and real tests. Do not label future features LIVE.

## 1. Reality check — what is correct and what is not
| Area | Actual verified repository capability | Remaining user-visible/technical gap |
|---|---|---|
| Mixer geometry | Compact 10CH + MASTER, faders, PAN, MUTE/SOLO, native meters, BTN-180 tokens | No integrated per-strip I/O + SENDS/FX/ROUTE mode bank with DAW-like clarity |
| CH1 VST3 | Real original multi-insert chain, editor, reorder, bypass ramp, DearVR Orbit; integrated into one 10CH callback | Recent GUI regression hid inserts in some native active modes; protect forever |
| CH2–10 and 4 AUX | One native stereo-compatible routed VST3 per channel/aux, prepared instance and revision ACK | Not independent eight-insert chains; no plugin-count visual fakery |
| Input patch | Actual DIRECT device enabled physical ports; mono/stereo mapping, 10×8-page matrix, ACK/device epoch | Input selection not simple on main mixer; setup still too reliant on unattractive raw JUCE dialog |
| Internal sends | Up to 32 bounded CH→CH/CH→AUX PRE/POST edges; directed graph validation, 10×4 AUX grid | No clear channel send-fader/returns/mix bus experience for daily users; PRE/MUTE/SOLO policy and parallel alignment need explicit qualification |
| Output patch | `MAIN L/R` **read-only**, physical output port display in status | No arbitrary AUX/Group/Direct→hardware patch; no safe 8-output bus-to-pin matrix yet |
| Show / portable | Native DRY Show save/browse/recall, portable studio and catalog groundwork | Full VST3 stateful Show recall, physical remap/rollback, routing+FX atomic restore unfinished |
| Audio safety | DIRECT source gating; inactive or unpatched is silent, MUTE for structural APPLY, epoch/revision ACK | Actual 0-FX ASIO/WASAPI crackle and low-end hardware FAT **not qualified** (issue #15); PDC and output underrun evidence incomplete |
| UI performance | Existing focus on hover-free, no blur, minimized repaint | Must prove new selector/routing visibility on integrated GPU, not just RTX-class machines |

**Historical note:** `README.md`, original `ARD_AR_MIX_LIVE_FX_HOST_v0.3.md`, some P1A/P1H handoffs describe CH1-only or optional LAB *at their historical baseline*. They are not current product truth. Current SHA + latest code + this ADR win when documents conflict. Old PR #115/#117/#113 and others must not be bulk-merged onto current graph.

## 2. Information architecture: fixed mixer + independent workspaces
Top navigation (operator order): **MIX | CHANNEL | FX RACK | ROUTING | SHOW | SETUP**. Optional PLUGINS / SIGNAL CHECK / diagnostics are utility tools. In native mode every button is wired; in standalone browser preview, clearly show `DEMO / NO HARDWARE`. Never promise a live stream or plugin signal path from CSS-only click.

### MIX (daily operation)
- **Permanent geometry:** 10 source strips (horizontal scroll only if needed), 4 AUX FX returns in a distinct selectable bank, and MASTER fixed/visibly identifiable; channel name, color, pre/post meter, mute/solo, fader, pan, native FX row and source/output summary.
- **Strip section bank:** `FX` / `ROUTE` / `SENDS` (three clear mode buttons, same physical size and BTN-180 family) swaps ONLY a fixed-height upper section of the strip. Faders/meters/channel ID/MUTE remain stable. Default `FX`; `ROUTE` shows selected input name/format and MAIN/Bus destination; `SENDS` shows focused AUX send rows with level, PRE/POST, enable. Users can switch modes globally or per selected channel; preferences may persist but never edit native topology simply by viewing.
- **Single-channel focus:** clicking a channel heading opens CHANNEL inspector with left-to-right `INPUT → INSERT FX → EQ/DYN (only if implemented plugin) → AUX SENDS → OUTPUT`. Use actual VST3 editor rather than fake generic EQ that sounds different from installed plugins. A compact inspector may be docked below mixer; avoid multiple permanent heavyweight canvases.
- **Header I/O overview:** `ASIO/WASAPI: <verified device> · <input count> in / <output count> out`; a `DEVICE` action opens custom I/O setup. Always distinguish `MAIN mix destination` from `physical output pair`.
- **Output soundness:** changing `ROUTE` or `SENDS` creates a visible amber DRAFT. Only native ACK sets green LIVE. Safe commit while output MUTED, unchanged live graph on any failure. Closing a panel never commits.

### FX RACK (processing workspace)
Independent workspace, never `ROUTING` disguised as FX controls. CH1 exposes all actual 8 supported native slots, with Add/Replace/Editor/Bypass/Move/Remove/Orbit as applicable. CH2..10/AUX expose only their actually implemented single routed insert, with accurate `ADD/REPLACE → DRAFT → APPLY (muted) → ACK` and disabled extra slots clearly labeled—not five simulated ADD keys. When multi-insert is delivered all channels adopt the SAME shared rack controller, not a second renderer.

### ROUTING (advanced patch workspace)
`INPUT PATCH`: rows=10 native channels and columns=negotiated enabled physical input ports, at most 8 visible columns per page, label with real ASIO/WASAPI port ID/label. `M` mono, `L` and `R` stereo; assign/clear by click, keyboard and optional drag, not a drag-only UI.
`INTERNAL`: separate group of `CH → AUX 1–4`, `CH → CH BUS`, and eventually `GROUP → MAIN`. PRE/POST fader/send inspector; show selected channel's sends and receiving bus. Dedicated `WIRE VIEW` **only for focused source/destination**; no 10×4×10 permanently painted cubic-Bezier wires over faders.
`OUTPUT PATCH`: current MAIN L/R read-only until the native physical output-matrix milestone; then rows=validated source buses and columns=negotiated physical output pins with stereo pairing, per-pin ownership, output gain/polarity policies, protection/silence. Never provide an enabled clickable assignment that the backend ignores.
`ADVANCED`: optional graph diagnostics show selected signal path `IN3 → CH2 → PRE AUX1 → AUX1 FX → MAIN → HW OUT1/2`, collapsed to one or two selected paths to avoid spaghetti.

### SETUP > AUDIO I/O (custom ArSonMix UI)
**Do not expose raw JUCE's generic audio setup as the daily UX.** JUCE `AudioDeviceManager` remains the audio transport and capability provider initially. Implement original ArSonMix GUI over queried backend device types, stable identities, negotiated rates/buffers and enabled input/output bitmasks. A native JUCE fallback dialog may remain for advanced troubleshooting until full driver mode negotiation is verified. Two tabs:
1. `DEVICE`: ASIO/WASAPI endpoint, negotiated mode, rate, period, capture/output devices, DIRECT vs independent clock. NO "ASIO" label unless real driver and setup reports ASIO; no fake host IO.
2. `PORT PATCH`: `INPUTS` or `OUTPUTS` rows of logical mono/stereo source/destination and columns of **actual enabled** device pins. Select `M/L/R` as Studio One examples, with dynamic disabled/unsupported ports, port labels, output pair constraints, duplicate fan-in/out policy and test tone only when explicitly safe. Treat selecting physical `Input 7` as source mapping, not as opening the seventh audio device.

## 3. Audio graph: single deterministic source of truth
```text
REAL device ports / enabled mask
       │ (MONO / STEREO / BUS)
       ├─► CH1 INPUT → CH1 plugin chain [existing 8 slots] ─┐
       ├─► CH2 INPUT → native CH2 FX (currently 1 slot) ────┤
       └─► CH3..CH10 INPUT / internal BUS receiver ─────────┤
                       ┌─────────────────────────────────────┘
                       ▼
                post-insert pre-fader TAP ─────┐
                       │                     PRE send with own gain/pan
              channel fader/pan/mute/solo     │
                       │                     │
                post-fader TAP ───────────────┤ POST send with own gain/pan
                       │                     │
             explicit MAIN assignment        ▼
                       │             CH→CH BUS / AUX1..AUX4 FX returns
                       │                     │ (each return processed once)
                       └─────────────────────┴──► MASTER stage
                                                        │
                                      hardware output PATCH (currently MAIN L/R)
                                                        │
                                             negotiated render device
```
- **One directed graph / one audio callback render path.** Source fan-out to several channels only by explicit request; bus fan-in is summed into fixed preallocated buffers. Distinguish physical `AudioInputPort`, `InputChannel`, `FxBusReturn`, `GroupBus`, `MonitorBus`, `MasterBus`, `HardwareOutputPort`; avoid "channel 11 = AUX1 = output 1" type aliasing.
- Existing `engine/core/NativeDryRouteCommand.h`, `PreparedMixerBlock.h`, `ChannelSendPlan.h`, `AuxBusPlan.h`, `LiveChannelFxGraph.h`, `RealtimeRoutePublisher.h`, `src/LiveDryRoutingLab.h`, `src/LiveAudioEngine.h` remain the baseline. Extend prepared graph via typed `RoutePlan` / `OutputPatchPlan` (may be internal structs rather than new layers), not parallel independent audio engines or new per-channel OS callbacks.
- Preserve current 32-send bound until capacity review (not "32 physical channels"); internal source buses and AUX return index are **distinct from physical ports**. Loop validation must cover CH→CH, CH→AUX, future bus→bus, plus accidental hardware-to-loopback monitor feedback. Self/cycle invalid before any publish.
- **PRE/POST exact semantics:** PRE = after channel inserts before channel fader; POST = after channel fader/pan. Define and test source MUTE/SOLO interactions explicitly against actual current DSP so PRE does not accidentally bypass a musician's mute. Selecting PRE must not silently change channel inserts or route to an arbitrary hardware output. Later optional INP/PRE/POST/POSTPAN taps require real extra buffer and PDC gates before an enabled UI.
- Channel `MAIN` routing is explicit to prevent double-feed. AUX return to MAIN is not an independently mapped headphone output. Master mono/downmix is done once at final output stage, with existing `MasterOutputMonoPolicy`, no duplicated sum.
- PDC for every route to a shared sum point: prepare plugin path latency off RT, align parallel dry/wet and bus returns with bounded delay buffers. Until implemented keep existing zero-latency native routed plugin guard; do not claim time-aligned returns for arbitrary latency-inducing VST3.
- Future GROUP/DCA/CUE/MATRIX must be separate typed semantics: Group is audio sum/processing; DCA is control grouping *not* an audio bus; CUE/monitor needs safe independent output source; MATRIX is a downstream mix bus. No standalone cosmetic buttons without engine ownership.

## 4. Device and output safety (non-negotiable)
- Device catalog uses backend-reported actual `deviceEpoch`, source count, port identity, input/output masks and clock domain. No fixed number of 8 inputs or "every laptop has four output pairs". If user selects another device, **MUTE first**, invalidate old port references and all pending drafts, preflight capabilities, rebuild off RT, then require deliberate re-patch/confirmation. Do not silently map old input indices to newly attached devices.
- A physical OUTPUT PATCH plan must distinguish `MAIN LR → HW 1/2`, `MON 1/2 → HW 3/4` and `AUX 1 → HW 5/6` with actual 6-output interface. On a 2-output interface, show MON/AUX physical patch **unavailable**, while internal AUX→MAIN remains supported.
- Reject missing/disabled ports, invalid sparse masks, stereo L=R, incompatible sample/clock profile, unowned endpoint, unsafely summed competing outputs, graph feedback, unsupported direct monitor, invalid sound card format and stale `epoch/revision`. Never auto-enable monitors or auto-unmute on APPLY/recall/recovery. No "SAVE" of unsupported live routes.
- `JUCE backend` and `custom GUI` are separate questions. Keep JUCE device engine until a measured reason to replace it; removing generic JUCE setup *visuals* does not require new ASIO/WASAPI drivers or inventing hardware capabilities.
- Hardware sound continuity issue [#15](https://github.com/masarray/arsonmix/issues/15) remains P0: OUTPUT_ONLY_TONE, CAPTURE_ONLY and DRY_THROUGH 0 VST, headphones low level; gather full diagnostic report. A green CI isn't a no-crackle certificate.

## 5. Transaction protocol — no optimistic LIVE
Reuse current `arMixAction{action:"routingDryApply"}`, `arMixRoutingLabAck`, `arMixStatus.dryRoutingLab` and FX presenter bridges `__arMixStageRackFx` / `__arMixApplyRackFx`. **Do not invent a second routing event bus in UI.** Implement a typed internal command envelope (proposed next version) with `requestId`, `deviceEpoch`, `expectedRevision`, `inputChannels[]`, `sends[]`, `auxReturns[]`, `outputPatch[]` and `capabilityVersion`. Only add fields when the native parser, validation tests, UI and persistence all consume them.

```text
UI: input/FX/send/physical output change → DRAFT (amber) in one shared editor store
       ↓ [no audio mutation]
UI preflight → check MUTE, valid IDs, current epoch, channels, sends, visible capabilities
       ↓
native command thread → revalidate driver mask, revisions, feedback DAG, bus types,
                         catalog identities, plugin layouts, PDC and output ownership
       ↓
prepare plugin graph + fixed buffers OFF AUDIO CALLBACK (reuse existing instances)
       ↓
single block-boundary atomic publish, old graph retained for safe retirement
       ↓
native ACK {ok, requestId, epoch, revision, acceptedRoute, actualPhysicalPortMap}
       ↓
UI replaces LIVE snapshot; rejection shows reason + keeps original working graph
```
On window switch, route draft persists in its own model (never content-only), and `CANCEL` discards. Every negative outcome produces an ACK (including backup busy, missing device, stale revision, invalid plugins). Cancel native prepare with generation token when endpoint changes. Do not deduce success from the command being emitted. No UI-side routing applied to independent direct/fallback callbacks.

## 6. Presenter architecture and interactions
- `web/app.js`: owns navigation, stable MIX strip geometry, focused channel and local view mode ONLY. Remove routing-side interception from `FX RACK`; retain working simple `data-view` and new `data-strip-mode`.
- `web/bridge.js`: owns native FX rack, catalog picker, plugin editor/Orbit actions and mixer FX presentation. CH1 eight inserts stay visible when `dryRoutingLab.active`. Other strips show real single native insert plus clearly disabled non-capable slots. No full rack innerHTML rebuild on each 12Hz native meter/status event.
- `web/monitor.js`: owns advanced native I/O matrix, valid port labels, input/source draft, AUX send draft, physical output matrix **when truly implemented**, route preflight and native ACK. Share a small route view-model/protocol with FX presenter; **not** a second graph.
- `web/plugin-ui.js`: catalog virtualization and keyboard-accessible favorites/picker. No lying "CH2 unavailable" when a verified native routed insert exists; selection is DRAFT requiring MUTE, not a CH1 `addCatalog` command.
- `web/styles-src/*` -> generated `web/styles.css`: source-owned CSS and BTN-180 consistent. Static gradients/inset borders are okay; NO CSS filter/backdrop blur, hover triggered whole-console repaint, constant rack DOM replacement, 1000 DOM wires, new FX slot animation loops. Keep meter compositing budget and measured pointer latency on standard integrated GPU.
- UI `ROUTE` mode is a *view* of the same source registry as `SETUP > PORT PATCH`. Patching from a strip highlights corresponding matrix row; patching from the matrix updates strip after native ACK. Keyboard navigation/accessible labels are mandatory. Drag-drop optional; clicks and explicit `APPLY` always available.
- A **focused wire overlay** is generated from accepted native edge list only when a user chooses `TRACE ROUTE` or selects source and destination, draw max 1–3 active paths and re-render only on selection, geometry change, or ACK, not on per-frame meter update. No always-visible wires crossing all channel faders.

## 7. Hardware I/O projection type contract (proposed)
```typescript
type DevicePin = { endpointId:string; pinId:string; callbackIndex:number;
   direction:"capture"|"render"; enabled:boolean; label:string;
   clockDomainId:string; supportedLayouts:("MONO"|"STEREO")[] };
type ChannelInput = { channelId:string; mode:"NONE"|"MONO"|"STEREO"|"BUS";
   leftPinId?:string; rightPinId?:string; sourceBusId?:string };
type SendEdge = { sourceId:string; destinationBusId:string; tap:"PRE"|"POST";
   gainLinear:number; pan:number; enabled:boolean };
type HardwareOutputEdge = { sourceBusId:string; leftPinId:string;
   rightPinId?:string; mode:"MONO"|"STEREO"; gainLinear:number };
type RouteSnapshot = { deviceEpoch:number; graphRevision:number;
   inputChannels:ChannelInput[]; sends:SendEdge[]; outputEdges:HardwareOutputEdge[] };
```
Stable pin identity is an **evolution** from current integer `port` callback index; it must be sourced by JUCE/backend enumeration and reconciled against latest enabled callback index before audio. Never use friendly port label as unique ID. Output pin ownership needs explicit channel-safety validation (one logical mixer output per physical pin by default; engineered matrix sum is separate feature). User-created logical port names sit on top of stable backend IDs.

## 8. Modules and zero-naive implementation method
Before new native code, inspect `LiveDryRoutingLab` / `NativeDryRouteCommand` / `AuxBusPlan` / `PreparedNativeMixerSession` / `Main.cpp` for the exact reused facilities. Plan a new feature as **data model → validation → prepared graph → UI presentation → ACK round trip → FAT**. Never start by drawing disabled buttons or duplicating business rules inside UI.
1. Pure C++ testable types for device mask mapping, output ownership, stereo layout and send graph; determine invalid states before wiring controls.
2. Transaction envelope and prepared graph adaptation, with bounded static memory and safe instance lease/retirement. No allocation, filesystem, mutex, browser dispatch, logging, factory call, graph deletion or device enumeration from callback.
3. One native command path and one authoritative status/ACK. Prefer incremental typed extensions to existing `routingDryApply`, not a parallel host/mixer.
4. GUI integration reusing source CSS and existing button material. The MIX strip must not lose FX nodes in any native mode, and Show restore must not replace its UI with a different one.
5. Deterministic tests first, then one consolidated exact-head Chromium + native MSVC CTest/ZIP if C++ changed. Only when green release Windows artifact for real user tests.

## 9. Major deliverable gates (not tiny PR churn)
| Gate | Vertical slice; output user can test | Non-negotiable acceptance |
|---|---|---|
| **G0 — Design freeze** | This ARD+PRD, conflict/stale docs banners, owner map and latest SHA reconciliation | No contradictory CH1-only/10CH status in entrypoints; no hidden routing requirements |
| **G1 — MIX/FX/ROUTE restoration** | One integrated console: six visible insert rows, preserved CH1 eight-slot real FX and CH2–10 truthful slots; MIX upper `FX/ROUTE/SENDS` toggle, selected-channel inspector; no overlay replacing rack | CH1 multi-FX remains after 10CH native activates and after navigating MIX→ROUTING→FX; FX actions not going through fake routing; native ACK draft model preserved; screenshots 1366/1440/1680; fast pointer |
| **G2 — Custom hardware I/O setup & output truth** | Device tabs and native port matrix `INPUTS/OUTPUTS` visually replacing ugly JUCE UI; main-strip source controls; MAIN L/R physical out identity readback; 2/8-output sparse device FAT | Names/indices from actual device mask, mono/stereo L/R, stale hotplug rejection, no fake output, no surprise sound; until native output patch is real, controls read-only |
| **G3 — Real AUX routing workflow** | MIX SENDS bank, 10×4 AUX matrix, focused send drawer, four FX return strips and PRE/POST send controls, signal trace; one session, shared ACK | Test DRY + vendor VST3 returns, 2+ sends at once, fader PRE stays independent of fader while POST follows it, mute/solo policy proven, no duplicate MAIN or feedback, no extra callback; user hears correct wet path |
| **G4 — Real output patch & bus expansion** | Native `OutputPatchPlan` plus UI rows=MAIN/qualified bus, cols=device outputs; first real independent AUX/MON physical output on interface with sufficient pins; later typed GROUP/CUE/MATRIX | Two-output device refuses unavailable pins; 4/8-output tests prove independent simultaneous destinations, mute/safety, no feedback, exact READBACK and rollback, no phantom AUX output |
| **G5 — Unified multi-insert + PDC + full Show** | Same multi-slot FX Rack API for every capable channel and AUX, phase-aligned buses, stateful portable session, crash/device recovery | Stable vendor instance leases, no duplicated plugin state, bounded PDC, verified Show restore plugin states and output mapping, transactional failure rollback |
| **G6 — Release quality / hardware FAT** | Exact-source Windows zip, field diagnostic report, 0-VST and multibus real hardware pass, low-end PC UX | Qualified only with independent physical audio evidence; #15 remains open until criteria pass |

**Milestone sequencing:** G1 & G2 are first executable *user-facing* release, G3 follows as one complete routed audio path with screenshots and executable artifact. G4/G5 require real native prepared graph changes, not passive GUI. Do not wait for every future feature before giving a useful Windows FAT build.

## 10. Acceptance matrix / failure injection
- **UI navigation:** MIX→FX Rack→ROUTING→MIX must preserve loaded FabFilter/dearVR or test VST3 identity, order, bypass and Orbit; no `+ ROUTE/FX` replacement. Probe 0,1,8 loaded insert counts.
- **Device:** 0 input, 1 mono input, 2 input/2 output, 8+ input/8 output, sparse mask, disconnected ASIO, WASAPI shared, rate change, ASIO restart, swapped stereo pair, unavailable driver. No fabricated pin in any view.
- **Native graph:** 10 independent sources, 4 AUX, max 32 sends, PRE/POST per source, two sends same bus, fanout/fanin, no path processing twice, mute/solo, main-only and sends-only, cyclic CH→CH and AUX→CH rejection, master mono after sum, untouched old graph on bad request.
- **FX/PDC:** real plugin initialization failure and multi-mono/stereo layouts; zero/positive plugin latency; dry/wet phase alignment (once PDC exists); identity on reorder/graph swap; chain and Orbit stay active across I/O navigation.
- **Output:** 2-output refuses 5/6 and 7/8; 8-output actual main + aux/monitor receive independent audible signal; no auto-copy of MAIN to all render pins; no implicit unmute/feedback loop; runtime output masks readback from native.
- **ACK and lifecycle:** racing user edits, repeated APPLY, stale device epoch/revision, busy backup, inactive renderer, plugin removed during preparation, rejected Show recall, crash/reopen, independent callback recovery. Every structural request resolves once (success OR negative ACK); UI avoids forever `APPLYING`.
- **Performance:** at 1366×768, integrated GPU, mouse movement across racks 60 sec while meter telemetry active; p95/p99 pointer and long-task sampling, no compositor blur, no scene-wide mutation on 12Hz status. 48k/128 and realistic chain must be measured, not asserted on laptop GPU.
- **Hardware:** muted start, low headphone level, 0FX tone/capture/dry-through, CH1 VST+CH2/AUX, PRE/POST send, MAIN L/R routing, glitch MARK and full report. Required PASS/FAIL on actual device before "live ready."

## 11. CI and parallel-thread contract
1. Read latest SHA and open PRs before editing. Rebase from exact main; **never bulk merge stale #115/#117/#113/#128** or replace new GUI with older "approved v0.8" HTML.
2. Ownership: `web/app.js` + `web/index.html` navigation/MIX; `web/bridge.js` FX; `web/monitor.js` input/advanced patch; `engine/core/*` prepared routing and sends; `src/LiveAudioEngine.h` single callback integration; `src/Main.cpp` native commands. Any cross-file change requires the integrated smoke gate.
3. Do not create full separate JS and C++ routing models; typed view-model serializes into existing single native route transaction. Do not move 6 FX inserts into Routing or code a new FX engine behind it.
4. Run fast local JS parse, targeted deterministic Node/C++ tests. Submit **one meaningful vertical-slice PR** when coherent, not tiny repeated force pushes. Run existing Chromium and Windows native workflows only as appropriate; no new redundant CI workflow, no concurrent redundant full Windows builds. Exact-head CI must pass before merge; artifact built on merged main with provenance.
5. Document sourceCommit, screenshots, checks, physical FAT result, known unsupported paths. Distinguish `MERGED`, `CI PASS`, `FAT UNQUALIFIED`, `NOT BUILT`. Never declare "hardware ready" on code/test evidence only.
6. Any contradiction: **this v2 ARD + current PRD + newest main source win for new mixer UX/IO**, Smart Audio ARD and current tests win for RT safety, plus the user supplied examples win for user experience intent. Historical dated implementation notes are evidence, not current directives.

## 12. Reference methodology (licensed semantics only, no copied UI)
- Waves eMotion LV1 Classic official guide: https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html — channel window has Input, Plugin Rack, Aux Sends and Output; separate patch views for Input/Output/Internal; hardware busses differ from internal FX/MON.
- Adobe Audition official documentation: https://helpx.adobe.com/ca/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html — mixer modules for inputs/outputs/effects/sends can be shown/hidden.
- PreSonus official Song Setup guide: https://support.presonus.com/hc/en-us/articles/360045189852-How-do-I-record-the-mix-back-to-an-audio-track-like-in-Pro-Tools — inputs/outputs, sub-output buses, explicitly configured output source.
- User's nine screenshots (2026-10-30) are **design input**, not software assets to redistribute or embed. Their intent: eMotion fader bank/mode switch, mono/stereo I/O grid, selected channel left-to-right control, transparent bus fan-in and optional focused cable trace.

## 13. First engineering handoff
Start with [EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md](EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md). **Implement G1+G2 as one combined meaningful product milestone where possible.** Preserve working native CH1 insert rack and 4 AUX return engine. Make input/output available from main window and custom matrix; no fake physical output selector. First artifact must be an actual verified Windows executable zip plus test evidence, not only a webpage screenshot.
