# ArSonMix — Hotkey Register & FL Studio External OUT
## Verified UX Research + Engineering Execution Blueprint — 2026-11-11

**Status update 2026-11-11: H1 MERGED (#536), H2 MERGED (#551), H2 integration Windows 86/86 CTest PASS; O1/O2 PHYSICAL MULTI-OUTPUT NOT IMPLEMENTED, REAL ASIO FAT PENDING.** For O-only work use [new-thread execution handoff](MILESTONE_O_MULTIOUTPUT_NEW_THREAD_20261111.md). The original research/acceptance contract remains binding.
**Audited source:** main a07b16e1c6f04473d328ddd1deab607774375ee3; refresh main and PRs before coding.
**Authority:** AGENTS.md, architecture/README.md, MAINTAINABILITY_EXECUTION_V1_20261111.md, FEATURE_EXTENSION_PLAYBOOK.md. Preserve exactly one assembled /preview/ GUI, one installed ChannelId/EdgeId native PCM graph and JUCE callback, one native ACK/readback truth. This document is a product feature workpack, not a new engine proposal or an additional approval gate.

## 1. User evidence and external benchmark

User comments on ArSonMix release video:
- @znyberz: configurable mute/solo shortcuts usable while mixer minimized or another app is foreground; serious live operator/background use.
- @ALMUHIBBINNUSANTARA: compares REAPER and FL Studio multi-input/multi-output, wants amplifier-facing multiple physical outputs for 2/3/4-way DLMS-style tasks; do not confuse desired result with need for multiple MASTER faders.
- @wijayanto7188: controllable eight output channels for speaker management.
- @AmbarSulistyawan: multi-output.
- @XionelWarzuki: access Maono specific headphone/loopback outputs, not merely ASIO device name.
This qualitative evidence justifies work, not market share/percentage claims. Two-independent-soundcard input aggregation and full speaker crossover are distinct workstreams, not hidden deliverables here.

### Source-of-truth research (official, URLs verified 2026-11-11)

| Verified third-party feature | Documentation | Adopt / do NOT imitate |
| --- | --- | --- |
| FL Studio Mixer has External Audio OUTPUT on every Mixer Track, one stereo external output per track, ASIO ports from driver; Master output required for Master audio; same hardware pair may receive multiple tracks. | https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm | Compact per-selected-channel/BUS External OUT: (None) + exact verified hardware stereo pairs. No giant multi-MASTER card. |
| FL Studio External OUT can coexist with internal Master/track sends; separate command Route selected to this track only disconnects Master sends intentionally. | https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm and https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm | Changing External OUT must NOT change WIRE / internal primary routing. Hardware-only route requires explicit internal disconnect. |
| FL Studio right-click Link to controller, Last Tweaked, Browser > Current project > exposed automatable parameters; per-project and global controller links have different scopes. Note: this is MIDI/internal-controller linking, NOT proof FL Studio has global Windows keyboard shortcuts. | https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/automation_linking.htm and https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/browser.htm | Familiar right-click target capture, Last Touched fallback, searchable parameter list and distinct session/global shortcut scopes. Implement keyboard separately. |
| REAPER track routing/hardware sends, Routing Matrix and Action List keyboard shortcut workflow are a secondary flexibility reference. | https://www.reaper.fm/userguide.php | Keep advanced Audio I/O OUTPUTS diagnostic/matrix view; do not copy REAPER's full N-send editor as initial quick External OUT. |
| Win32 RegisterHotKey delivers WM_HOTKEY system-wide; MOD_NOREPEAT suppresses key repeat; registration can fail for conflicts; MOD_WIN OS-reserved, F12 debugger-reserved; UnregisterHotKey releases registrations. | https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey and https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unregisterhotkey | Native Windows keyboard backend; never rely only on WebView2 keydown for background operation. |
| VST3 IComponentHandler3 vendor context menu is optional. Vendor parameters expose ParamID and flags kCanAutomate/kIsReadOnly. | https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IComponentHandler3.html and https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ParameterInfo.html | Optional vendor menu if JUCE supports it; always provide host-side Last Touched / Parameters fallback. Never silently bind parameter by ordinal alone. |

Do not copy vendor artwork, CSS or screenshots into repository. The official documentation and real /preview/ are UX observation sources; reproduce the WORKFLOW, not proprietary assets.

## 2. Grounded ArSonMix source audit and ownership

- web/approved-native-context-menu.js already owns a native-only right-click handler for Rename/Delete and DearVR Orbit. Extend it. Do not create a second document-level context-menu handler or lose existing behavior.
- web/approved-native-bridge.js and approved-native-contract.js are transport/projection; current primary OUT points to MAIN/internal ChannelId nodes, not arbitrary hardware output.
- web/mixer-io.js and monitor.js already expose DEVICE, INPUTS, OUTPUTS, verified physical ports, and MAIN/MONITOR hardware patch; adapt those existing views rather than making another Audio Setup.
- engine/core/DynamicChannelRoster.h has stable ChannelId and EdgeId, and native FX lease maintains actual plugin instance identity across FX moves. Use those, never strip index/FX slot.
- src/Main.cpp has real JUCE PluginEditorWindow and native parameter inspection for DearVR Orbit. Third-party editors are not WebView DOM; a JS right click does not intercept a vendor plugin window.
- src/NativeMixerSessionDocument.h separates durable mixer logical state from ephemeral HardwareBinding, includes .smix and logical-only sessions. Integrate versioned shortcut + requested output identity into that document with migration; never serialize PCM callback ordinals or mark a logical-only output physically active.
- engine/core/UnifiedLegacyProductionAdmission.h VerifiedNativeOutputs is currently MAIN L/R plus optional MONITOR L/R; src/LiveDryRoutingLab.h / DynamicNativeSessionPublisher.h / LiveAudioEngine.h own the real accepted render/callback. OUTPUT1 secondary WASAPI only copies MAIN stereo through a separate clock bridge (docs/OUTPUT1_FLEXIBLE_LIVE_ROUTING_20261031.md). No general BUS->hardware output claim yet.
- Historical fixed 10-CH M4 patches can still coexist; do not add another M4 output field or reject legitimate ChannelId routing by legacy MAIN/NONE rule once native graph is active. Preserve VST3, SOURCE ONLY and one callback authority.
- No code inspection or CI result is equivalent to verified ASIO hardware FAT. Related open issue owners: #412 live routing, #192 dynamic channel, #15 crackle. Main/PR status may change after this document.

## 3. HOTKEY PRODUCT CONTRACT: exact operator UX

### H-UX-1: right-click target -> Add Hotkey -> capture -> Save -> register

1. Operator right-clicks an ELIGIBLE identifiable GUI command: CH05 MUTE or SOLO, selected rack bypass, Player Play/Pause, named transport and other actual supported buttons. Existing contextual actions remain. Show Add Hotkey… only when native target/command is known and allowed. On a track header without a single obvious action show a short list of eligible commands, not an ambiguous "bind whole strip".
2. A compact premium draggable pop-card opens anchored near control with title ADD HOTKEY, canonical target (e.g. CH05 / MUTE), instruction "Please press Hotkey", a central captured chord preview, 1-line validation, Save/Cancel. It captures a full keyboard chord without executing currently registered ArSonMix mappings during the popup. Modifier-only keys are invalid; Esc cancels; Enter saves only when valid.
3. Save attempts a native typed registration and a transactional persistence write. On true ACK and later readback, register row appears; no optimistic browser-only success. On conflicts show existing target or Windows reservation, offer explicit replace of ArSonMix mapping, otherwise keep old working mapping intact.
4. Open Hotkey Register from existing utility/topbar (no giant separate page). Compact scrollable table/grid columns: Shortcut | Target (channel, control or plugin param) | Scope (Session/App) | Status | Edit/Delete. Search by key/target/plugin. Disabled/unavailable entries remain viewable, never silently reassigned or deleted. Delete unregisters and persists before row disappears.
5. Press registered shortcut -> native dispatcher runs the EXACT supported control operation as real GUI -> accepted state re-read -> visual MUTE/SOLO/FX indicator updates. No simulated mouse clicks, offscreen DOM, untrusted JavaScript or duplicate command bus.

UI density: match ArSonMix approved graphite styling, restrained cyan focus, existing font hierarchy and small type; no large bold cards, overflow, blurry gradients, or movement of SVG fader/FX rows. Popup provides keyboard focus and accessibility labels, not browser default menu.

### H-UX-2: background and collision policy (Windows)

- Register globally through RegisterHotKey(HWND, id, modifiers|MOD_NOREPEAT, VK) and handle WM_HOTKEY on JUCE message/control thread, independent of browser focus or a minimized window. Do not install keyboard hooks/collect arbitrary typing. Plugin editor in front and external app in front must not break a genuinely registered shortcut.
- Global chord must have safe modifier and non-reserved VK. Reject Win-logo shortcuts, F12, modifier-only chords, unsupported multi-step sequences. Warn about AltGr/keyboard-layout conflict for Ctrl+Alt; use key code + modifiers internally and display keyboard-layout-aware label. App-only keys can support non-global combinations away from text inputs; do not globally capture single letters.
- OS conflict: RegisterHotKey returns failure -> show conflict/unavailable, do not write REGISTERED. In-app duplicate: show explicit "Already mapped to CH03 Solo / Replace?" (default cancel). For reassignment stage new registration and persistence transaction with rollback; never silently orphan an old binding.
- Key held should toggle once; repeated event with request pending must be idempotent (requestId+mappingId and accepted state). Two presses should follow acknowledged state, not stale browser boolean.
- Rebuild registry on startup/session change. Unregister old keys when swapping sessions/removing mapping/shutting down. If other app grabs an old key before resume, report unavailable and preserve stored intent.
- App-wide shortcuts apply to application-safe actions; Session shortcuts reference that session's exact ChannelId or FX instance. Resolve precedence explicitly and show active conflict rather than shadowing silently. Missing target stays UNAVAILABLE and never controls same-index replacement channel.
- Dangerous actions (Delete track, changing hardware pins, destructive Save/Scene) are not instant global-hotkey operations; preserve existing confirmation/safety requirements.
- Secure desktop, different-privilege/app-captured keys and OS-reserved combinations cannot be guaranteed; disclose limitation, no false promise that EVERY key works globally.

### H-UX-3: plugin-specific hotkeys and honest VST3 behavior

- Built-in effects: their own control widgets know stable parameter/action descriptors, so right-click Add Hotkey directly.
- Vendor plugin native editor: IComponentHandler3 host-provided context menu is optional; JUCE VST3 host compatibility must be audited first. Only enable direct right-click on vendor parameter if actual native plugin exposes this capability and instance/parameter identity is verified.
- Fallback ALWAYS offered from a plugin/FX header: "Hotkey for Last Touched Parameter" (only after real captured automation notification) and "Browse Parameters…" (searchable, only automatable/writable exported entries). This copies a proven FL Studio workflow rather than fabricating interception inside third-party editor windows.
- Persist stable target: session ID + ChannelId + exact FX instanceId + VST3 catalog/class UID + verified vendor ParamID (built-ins use stable declared parameter ID), plus action semantics and capability. **Never** persist slot index, parameter list ordinal, UI name or vendor pointer as sole identity. Audit whether installed JUCE wrapper surfaces vendor ParamID; if not, use guarded identity+fingerprint and show REMAP REQUIRED on restore; do not fire a changed-index parameter.
- Boolean parameters can TOGGLE; continuous parameters require SET/STEP operation with real range, steps and clamp; ordinary Play/Pause is INVOKE. No arbitrary pulse/momentary without tested key-up delivery. Do not use parameter setValue from realtime callback or access destroyed vendor instance; invoke validated parameter setter through existing control/processor lease and native readback.
- Plugin missing/replaced/reordered/renamed: instance and parameter mapping follows verified identity or becomes unavailable. Reordering an FX slot must never point shortcut at a different plugin.
- Third-party plugin parameter changes can originate from plugin callbacks; last-touched observation must be realtime-safe (atomic index/notification, deferred control processing), avoid allocations/logging. Host-originated hotkey edits must not recursively generate a phantom last-touched record.

### H-ENGINE and persistent ABI

Introduce one small HotkeyActionRegistry / ShortcutService boundary on native CONTROL, not another browser or PCM engine:
- ActionDescriptor: stable commandKey, targetKind, optional ChannelId/FX instance/catalogUID/parameterID, scope, allowed operation, capability, display label. Must resolve only to an existing approved native dispatcher action.
- HotkeyEntry: version, mappingId, keyVK/modifiers, scope, action descriptor, enabled; validate length/size, reject unknown action or executable payload. Global app profile in atomically replaced settings, session mapping as additive .smix schema. Migration of old sessions is empty mapping, never loss of vendor VST3 state.
- Events: INTENT register/edit/delete -> native validation + OS registration + persistence -> ACK -> independent readback; runtime WM_HOTKEY -> resolve current session/identity -> approved existing native command -> ACK/readback. LOGICAL_ONLY has no fake physical action. Missing device or unadmitted graph cannot be bypassed by a shortcut.
- All error handling and disk writes are off realtime thread. Keep one source of truth between GUI button and hotkey command. Do not append a per-plugin special-case switch to the 6,000-line Main.cpp for each new mapping.

## 4. FL STUDIO EXTERNAL OUT: exact operator UX

### O-UX-1: fast one-selector output assignment

1. Select normal mixer INPUT, GROUP or FX RETURN. In existing selected-channel ROUTE surface show two clearly separate rows: INTERNAL OUT = MAIN/GROUP/SOURCE ONLY (existing WIRE model) and EXTERNAL OUT = None/current verified driver port pair. Default external assignment: None.
2. Click External OUT. List None plus **only** real enabled output stereo pairs from active same-device ASIO (e.g. Device / Line 3+4). Show real driver labels. Never fabricate HP/Loopback or a pair whose physical mask is disabled. Existing AUDIO I/O > OUTPUTS continues to show advanced port patch/diagnostics.
3. Select pair -> native control-thread device/epoch/pins/graph preflight -> prepare immutable output mapping -> safe de-click transition at block boundary -> native ACK then later matching installed graph readback -> status LIVE. On rejection show reason and retain old sound/route; do not fake success. If a particular patch cannot yet be made safely without manual mute, explicitly mark capability not ready until that path is engineered; do not secretly demand repeated MUTE/APPLY/ARM as everyday workflow.
4. **FL semantics:** choosing External OUT does NOT disconnect INTERNAL OUT. Want output to hardware only? User explicitly uses existing ROUTE ONLY/SOURCE ONLY (remove primary internal edge). The channel meter remains alive with its source/FX; MAIN contribution disappears but hardware direct path remains audible.
5. Save/Open: retain user logical output intent, but revalidate physical backend, output label, mask, stereo pair and device epoch before enabling. On different/missing device show NEEDS REMAP, do not silently switch to other speakers or auto-unmute. Previously verified same setup should remain frictionless without a recurring Monitor wizard.

Initial UX intentionally follows FL Studio: **one selectable stereo External OUT per channel/BUS**, not an entire REAPER many-hardware-sends grid. Do not implement full DLMS crossover or multi-soundcard asynchronous aggregation in this milestone. Existing MAIN and MONITOR physical assignments remain their own typed paths.

### O-UX-2: tested 8-output journey

On a REAL driver-reported 8-channel ASIO interface (fixture, not default automatic patch):
- MAIN post-MASTER processed PCM -> physical 1/2
- GROUP A post-group FX/fader PCM -> physical 3/4
- GROUP B -> physical 5/6
- GROUP C -> physical 7/8
Each BUS receives its own selected channels. FX instances render ONCE, not once per route. MAIN FX does not incorrectly color the direct output bypassing MAIN. 2-output devices expose no invented 3/4; 4-output devices can perform MAIN 1/2 + BUS 3/4.

MONITOR already may own a separate physical pair (e.g. 3/4). If BUS wants same pins, resolve according to explicit destination ownership policy. Do not let BUS steal MONITOR invisibly. No fake third hardware output from OUTPUT1 secondary device, which is a same-MAIN stereo copy with distinct clock.

### O-UX-3: valid fan-in, intentional parallelism and collision safety

- FL Studio permits multiple mixer tracks sent to one stereo output. Therefore the engine may sum intentional, explicitly routed contributions to that one physical destination, once per audio block; do NOT create separate device callbacks per track. UI should show shared destinations plainly.
- A source can reach HW1/2 through CH -> MAIN -> HW1/2 and simultaneously CH direct -> HW1/2, doubling sound. Detect this graph ancestry/path collision before apply and show exact warning with deliberate confirmation; do not silently sever WIRE, silently attenuate, or claim duplication impossible. Hard feedback loops/rejectable graph cycles remain fail-closed.
- Existing MAIN/MONITOR physical ownership rules currently require disjoint pin pairs. Decide and implement an explicit policy for these exclusive roles versus shareable direct outputs *before* removing these invariants; a role collision can be rejected with "HW3/4 belongs to MONITOR" while independent ordinary direct sends to an explicitly shared destination are mixed deterministically.
- Physically new/unknown amp output should receive one contextual confirmation of exact device/pin; no arbitrary auto-route on hotplug. Already trusted same-device patch is streamlined with bounded crossfade/declick.
- Tap for External OUT is post-channel/group FX, fader and pan, before MAIN FX/gain. Muting/soloing actual source affects corresponding direct output. PDC and channel format must be proven in both direct and routed paths. A BUS direct sound can intentionally differ from its MAIN-processed path.

### O-ENGINE: reuse one PCM authority, no parallel renderer

- Create a bounded PhysicalOutputAssignment/OutputPatchPlan integrated with existing accepted ChannelId session. Separate (a) durable hardware-output *intent* keyed by stable ChannelId and selected device-profile/pair identity from (b) actual epoch-bound ASIO port IDs and callback ordinals. Physical output is NOT an internal WIRE ChannelId destination.
- Extend native output preparation at the current DynamicNativeSessionPublisher / LiveDryRoutingLab and VerifiedNativeOutputs integration boundary. MAIN/MONITOR stay supported, OUTPUT1 WASAPI Main-copy remains separate, and native immutable graph snapshot remains SINGLE.
- Control thread checks actual active pin mask and names, resolves sparse ordinal mapping (never assume L and R are numerically adjacent if driver doesn't enumerate that pair), checks structural FX/format/PDC/scratch budgets, collision and device epoch/revision. Prepare one fixed-cost destination->source sum plan and publish atomically.
- Realtime callback clears correct output buffers once, renders every FX channel once in existing topology, accumulates accepted post-FX/fader sources to mapped physical channels, writes MAIN and MONITOR without overwriting aliases, honors output mute/solo and de-click. No heap, mutex, waiting, VST construction, filesystem, JSON, UI calls or logs on audio callback.
- Add typed native commands such as SET_EXTERNAL_OUTPUT (name illustrative) including requestId, source ChannelId, deviceEpoch, graph/roster revisions and exact verified stereo destination pair. Native ACK is not by itself physical FAT; then independent snapshot must expose accepted output patch and mapping status.
- Extend session schema additively. Retain pre-existing .smix, vendor binary state, ChannelId/EdgeId, original MAIN/MONITOR assignments; logical-only edit can record desired destination but MUST NOT mark any physical output live. Device open/change/return must not destroy user mixer, plugin instances or WIRE.
- A routing edit to newly selected physical port must not auto-unmute intentionally muted output; normal trusted session can work without repeated fake monitor/setup buttons. Invalid pin or nonmatching driver must preserve previous accepted PCM and show Needs Remap.

## 5. Milestones: coherent vertical work and code ownership

### H1 — Native keyboard registration + original GUI context menu + persistence (FIRST)
Implement full right-click, capture, register, compact grid/edit/delete, app/session persistence, Win32 background hotkey + native action resolution, real control ACK and screenshots. Begin with CHANNEL MUTE/SOLO, rack bypass and supported player/app commands. Add source-nearest native and JS tests. No demo-only UI deliverable.

### H2 — VST3 parameter mapping + truthfully supported fallback
Audit actual JUCE VST3 host API for stable vendor ParamID and optional IComponentHandler3 integration. If vendor menu proven, support directly; otherwise Last Touched/Parameters from FX header is mandatory. Respect automation flags, move/reorder, plugin missing, id changes, no dangling vendor instance. One substantive implementation PR. If vendor API cannot recover identity, DO NOT save unsafe mapping; H1 and O1 remain useful independently.

### O1 — FL-style one stereo External OUT per track/BUS (native + approved GUI)
Add actual physical output destination plan/sum in one JUCE graph, native assignment command/readback, safe selected-channel dropdown, zero user reset wizard, session migration. Preserve MAIN/MONITOR/secondary and internal WIRE. Add deterministic synthetic 2/4/8-out PCM tests, stale device, collision, plugin render count, SOURCE ONLY/direct.

### O2 — Real hardware safety/FAT and interoperability
On real 8-channel ASIO output verify independent PCM 1/2,3/4,5/6,7/8 with different test tones through corresponding buses, 44.1/48 kHz and actual supported buffers. Validate 0-FX and vendor-VST FX, monitor collision, reconnect/hotplug, saved device remap, physical pop/click, PDC, thread budget, secondary WASAPI stream noninterference. Measure actual callback latency/xrun and CPU; do not invent values. Reconcile issue #15 crackle and #412 audio journey; hardware FAT not implied by CI green.

Do not mix these with unrelated Harmonice, release branding, maintainability huge refactor or speculative cross-platform work. Reuse #527 architecture seams; one meaningful integrated PR per shipped milestone, not dozens of mini-PRs or duplicate UI engines.

## 6. Deterministic acceptance specification

| Test ID | Input/action | Required result |
| --- | --- | --- |
| HK01 | CH05 MUTE right-click -> Add Hotkey -> chord -> Save | Compact register entry after native ACK; mapped CH05 really toggles |
| HK02 | ArSonMix minimized, OBS/another app foreground -> press global chord | Correct stable ChannelId toggles ONCE; held key no repeat |
| HK03 | Save OS-reserved / in-use chord | Explicit reject; prior mapping remains registered, no fake Success |
| HK04 | Reorder/rename CH05, reopen .smix | Same ChannelId is affected, not visual fifth row |
| HK05 | Delete/move/replace VST3 | Instance+parameter identity preserved only when proven; otherwise unavailable, never wrong plugin |
| HK06 | Save session hotkey, switch to another .smix | Session mapping inactive in other show; global app mappings behave as declared |
| HK07 | Capture popup active + existing shortcut, Esc, invalid key | No accidental execution, Cancel/Save semantics correct |
| HK08 | VST editor has no host right-click | FX Header Last Touched / Parameters works where automatable; unsupported marked unavailable |
| OUT01 | Driver has only active OUT1/2 | No fake 3/4; MAIN sound preserved; invalid assignments rejected |
| OUT02 | 4-output driver, BUS A -> External OUT3/4 | Real independent post-BUS FX PCM, MAIN1/2 unchanged |
| OUT03 | 8-output driver, MAIN1/2 + BUS A3/4 + BUS B5/6 + BUS C7/8 | Four distinct physical captures, each FX instance renders once |
| OUT04 | BUS A internal MAIN + external HW3/4; disconnect internal | HW3/4 keeps signal, MAIN no BUS A contribution, channel meter still works |
| OUT05 | Two source tracks intentionally target same hardware pair | One deterministic sum, no extra callback, same PCM not unintentionally double-routed |
| OUT06 | BUS requests MONITOR-owned pins | Clear safe conflict resolution or rejection, never overwrite silently |
| OUT07 | Stale revision/pins, missing hardware, output apply failure | Previous PCM preserved; session intent visible Needs Remap |
| OUT08 | Device restart/rate/buffer change, Save/Open | Stable ChannelId/WIRE/FX/output intent; no wrong physical port auto-arm |
| OUT09 | Master FX, per-BUS FX, mute/solo, pan and PDC | Correct post-BUS vs post-MAIN sound and plugin render count |
| RT01 | 0-FX and loaded VST3, secondary Main-copy active | No callback allocations/locks/extra device callback, measurable no-regression |
| UI01 | Native and /preview/ 1366x768, 1680x900, 1920x1080 | Compact register/selectors, real pending/readback, no fader/FX/Orbit regression |
| FAT01 | Physical 8-output loopback audition and logs | Real per-pin PCM, no self-reported "FAT PASSED" from unit tests |

Implementation tests must also cover duplicate requestId, keyboard focus in text inputs, plugin vendor hangs/missing params, saved corrupt hotkey JSON, duplicate mappings, sparse output pins, parallel edge ancestry, full-mute rollback and GUI first paint. Public release must not claim hardware or all-vendor compatibility without independent evidence.

## 7. Implementation handoff (must run against fresh HEAD)

1. Read AGENTS.md, docs/SMART_CI_RUNNER_WORKFLOW.md, docs/architecture/{README,MAINTAINABILITY_EXECUTION_V1_20261111,FEATURE_EXTENSION_PLAYBOOK}.md and this contract. Fetch fresh main SHA, active PRs, recent merged source, relevant tests. Inspect exact assembled /preview/ and native WebView2 build provenance. At audit time #512/#521/#529 had active branches; RECHECK, not assumed today.
2. Trace one complete current action: GUI gesture -> approved-native-bridge/contract -> Main native dispatch -> session revision -> DynamicNativeSessionPublisher -> LiveDryRoutingLab -> JUCE callback -> device output -> native ACK and later snapshot. Annotate what already works vs gaps in product PR. Do not create a separate architecture approval PR.
3. For H1 inspect web/approved-native-context-menu.js, Main.cpp, NativeMixerSessionDocument.h, NativeLogicalMixerWorkspace.h, existing editor/FX lease handlers and tests. For O1 inspect web/mixer-io.js, web/monitor.js, web/approved-native-bridge.js, engine/core/{DynamicChannelRoster,DynamicNativeSessionPublisher,DynamicPreparedMixerBlock,UnifiedLegacyProductionAdmission}.h, src/{LiveDryRoutingLab,LiveAudioEngine}.h and engine/device pin registry.
4. Implement a full vertical milestone with a few cohesive native/bridge/UI/persistence changes and tests. Avoid giant ad-hoc Main.cpp switches and second UI/contextmenu/PCM graph; extract narrow reusable control domain seams where necessary.
5. Run fast targeted tests. Then use smart exact-SHA CI: GUI+native typically UI plus **FAT** when a Windows test ZIP is needed, never also the duplicate native lane; docs-only changes need diff/link review and NO heavy CI. Follow owner-only dispatcher workflow; do not ask owner to click Actions. Distinguish queued/passed/failed accurately.
6. Do not merge untested native output behavior or mark vendor plugin right-click complete from mock. Physical FAT remains PENDING when no real rig. Handoff includes current main/PR SHA, paths changed, test IDs proven, artifact status and remaining blockers.

## 8. Explicit non-goals / anti-patterns

No 8 fake Masters, no invented output pins, no speaker crossover/limiter/DLMS certification, no new VST parameter interception hack, no global keyboard hook, no absolute "all buttons" guarantee for closed third-party vendor editors, no second native graph or callback, no WebAudio, no duplicate contextmenu controller, no JS optimistic LIVE, no per-channel plugin re-render for each output, no multi-device clock aggregation and no full REAPER matrix as first UI.

**Product acceptance:** one right-click Add Hotkey journey that remains bound to the *right* control; one FL-familiar External OUT per mixer channel/BUS that drives *real* physical ASIO pins, with internal routing unchanged unless explicitly edited.