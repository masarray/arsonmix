# ArSonMix PRD v5.2 — Operator-First Live Mixer Console
Date: 2026-11-01 | Updated: 2026-11-01, user-approved PR #278 one-console design lock (v5.2) | Status: **NORMATIVE PRODUCT TARGET, NOT SHIPPED FEATURE ATTESTATION** | Supersedes the workflow and priorities in PRD v4.1. Companion: [ARD v5.2](ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md).

## 1. Product promise and non-goals

ArSonMix is a native, real-time digital mixer and VST3 host for live stage, karaoke, podcast, online radio/broadcast and livestreaming. It must feel like an actual digital console on a normal Windows computer, using any compatible operator-selected physical or virtual input/output device. There is **no default mandatory OBS, virtual cable, audio interface brand, or fixed live setup**. Future cross-platform scope must follow platform-quality gates.

**NOT** a DAW timeline, waveform/audio editor, multitrack recorder, MIDI arranger, song sequencer, render/bounce utility or hardware simulator. Use Studio One / current Fender Studio Pro's **Console** functions, Waves eMotion LV1's **live routing and banks**, and Adobe Audition's **direct I/O / effects / sends visibility** as proven interaction patterns. Do not copy proprietary UI art, assets, DSP or brands. "Zero latency" means zero unnecessary added mixer DSP latency where physically achievable; device buffering, VST3 reported latency and clock conversion are nonzero and must be disclosed.

**Ultimate acceptance rule:** An operator can plug in a mic, add CH11 stereo, put genuine VST3 on CH2 and CH11, create a live vocal GROUP, add parallel reverb FX RETURN, send a monitor mix to a separate real output and continue hearing the mix, all from the same Console. **A mock strip, dry-only proof, link to ROUTING LAB or passing Chromium screenshot cannot satisfy this.**

## 2. Benchmark decisions — borrow the SIMPLEST capability, not entire DAWs

| Operator problem | Verified product pattern | ArSonMix product decision |
|---|---|---|
| See/change channel width | Waves LV1 input strip mono/stereo switch; Studio Pro track/Console mono/stereo format toggle | Always-visible per-channel M/ST selector. The **processing format** is independent of physical source pin count. A mono mic may feed stereo processing. |
| Assign input/output | Studio Pro channel I/O selectors; Audition selectable Mixer Input/Output sections; LV1 channel Route mode | Direct compact **IN** and **OUT** in every channel; one-click opens verified physical/software destinations, advanced grid is optional. |
| Add channel | Studio Pro Add Audio Tracks with mono/stereo and input/output selection | Pinned right-side **+ CHANNEL** skeleton; menu MONO/STEREO; creates a real independent silent native ChannelId, then optionally patch SOURCE. No fake CH11. |
| Move across large mixer | Studio Pro horizontally scrollable Console, LV1 Layers/Channel banks | Adaptive left/right bank arrows, horizontal scroll, searchable/channel-role filtered banks, always-available pinned MAIN. Do not force 10 visible strips or a fixed 10-channel engine. |
| Add effects | Studio Pro drag-to-insert, + Insert, ordered FX racks; Audition direct FX section | Same native **INSERT rack** on every channel/bus, including unpatched silent channels; drag or + to instantiate real owned VST3. No CH1 legacy privilege or inactive decorative rows. |
| Group channels | Studio Pro **Add Bus for Selected Channels**; LV1 Group route and layer | Select channels → **GROUP SELECTED** creates a new independent Group Bus, changes only selected primary OUT, preserves existing parallel sends/FX/meter/monitor/Show. |
| Shared reverb/delay | Studio Pro drag an effect to SEND to create FX Channel; LV1 AUX/FX and Sends-on-Faders | **SEND → + NEW FX RETURN** or drop a VST3 on SEND creates a dedicated wet FX Return and PRE/POST send; leave primary dry OUT untouched. |
| Stage monitoring | LV1 MON aux, Cue, Sends-on-Faders, independent output routing | Native MONITOR/CUE bus and selectable legal hardware/virtual destination; Send-on-Faders retains main fader values. No forced output pair other than deliberate user mapping. |
| Complex patch | LV1 filterable hardware/internal matrix | **Advanced PATCH** is an expert bulk tool only; not the mandatory path for an ordinary channel source, FX, send, or output choice. |
| Session portability | LV1 Show/Scene with Recall Safe, Studio Pro Scenes | **Show** saves accepted ChannelIds, plugin chunks, graph, logical I/O and scoped scenes; device remap preview before activation. |

Official evidence:
- Studio Pro channel format and I/O: https://fenderstudiopromanual.fender.com/en/Content/Recording_Topics/Audio_Tracks.htm
- Studio Pro inserts, FX Returns, Groups: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm
- Waves LV1 input width: https://www.waves.com/emotion-lv1-tutorial-3-3-channel-window-inputs
- Waves LV1 banks/groups: https://www.waves.com/emotion-lv1-tutorial-4-2-mixer-channels-groups-aux
- Waves LV1 route and send: https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html
- Adobe Audition direct Mixer routing/EQ panels: https://helpx.adobe.com/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html

## 3. Screen contract — Console is the home

### 3.1 Default workspace

One main MIXER view. Left compact rail: **MIX / INSERTS / SENDS / ROUTE** controls or collapsible detail; upper small toolbar: Audio Device, PATCH (advanced), Plugins, Show, safe output MUTE and honest I/O/latency/CPU health. No large cards, blurry glow, hover repaint storms or duplicate global navigation. Each input strip is compact but has:

**Name / color / role** → **IN [source]** → **M/ST [processing mode]** → **FX [ordered insert rack]** → **SEND [destination, PRE/POST, level]** → **OUT [primary destination]** → **MUTE/SOLO/PAN** → **one mono or two independent L/R meters** → **fader**.

Main fader stays sticky at right; role-filtered GROUP/FX/MON banks may be shown in their own contiguous section or collapsible secondary bank. Never show fixed CH1–6 single-meter and CH7–10 dual-meter groups as a permanent rule. A format transition must not remount active FX editors or jerk fader geometry.

### 3.2 Dynamic mixer navigation and creation

Rightmost **+ CHANNEL** skeleton, visible only when the native dynamic-graph capability actually exists. Click → quick menu: Mono Input, Stereo Input, Group, FX Return (advanced: Monitor/Cue). The created strip appears immediately **only after native accepted graph ACK**; pending local skeleton uses a distinct non-live status. On a narrower viewport, left/right arrows navigate one viewport-worth of channel widths, not necessarily exactly ten; mouse wheel+Shift/horizontal swipe and keyboard Home/End are alternative controls. Preserve selected ChannelId across bank/role/filter changes. Optional channel search, colored labels and bank favorites.

**Track** in reference DAWs is adapted to **Mixer Channel** in this live-only app; never imply recorded waveform tracks.

### 3.3 Popup/inspector discipline

Single-click a channel's IN/OUT opens a compact menu of **verified** sources/destinations. One click or drag on FX opens the shared VST catalog/editor. SEND dropdown shows existing destinations plus **Create FX Return**. A floating inspector may reveal detail, but must not push faders, obscure the selected FX slot or force users to navigate into Advanced PATCH. Current controller state should preserve the cursor and selected item after native ACK.

### 3.4 Honest state and microcopy

Distinguish **READY / PENDING / LIVE / MUTED / REQUIRES DEVICE CHANGE / REJECTED**. LIVE only from native accepted revision; signal status must additionally distinguish raw capture PCM, channel post-FX PCM, and physical output ARM. Do not show animated/demo meters as native. Errors tell operator what failed and what remains audible: e.g., "FX refused: this plug-in has no compatible mono layout. Previous mix continues." Do not expose STAGE→MUTE→APPLY→START as routine instructions.

## 4. Exact operator journeys and maximum interaction budget

| ID / gesture | Expected GUI | Required actual audio result |
|---|---|---|
| UX01: first launch / connect an input | Select compatible device/pins once in AUDIO I/O, click **USE** (or one first setup **APPLY**) | Internally guarded device/route ACK and optional ARM; never an additional START AUDIO button. Unpatched channels stay silent. |
| UX02: choose SOURCE | Channel IN → select real mono or L/R pins | Accepted same-format live source uses short native crossfade; changed topology is auto-prepared and committed safely. No manual full-mixer mute sequence. |
| UX03: change M/ST | Tap M/ST once on **any** channel | Native processing format changes, with explicit mono/stereo conversion, plugin-layout verification and truthful 1/2 output meters; no fabricated neighboring physical R pin. |
| UX04: add 11th channel | + CHANNEL → MONO/STEREO | Actual independent channel, stable ID, FX/send/OUT/meter, default silent/no source, and no theft of legacy CH10 slot. |
| UX05: plug in FX on silent CH2 | Click +FX / drag VST3 before choosing source | Owned prepared VST3 is present and editable, remains silent until valid input; never redirected to CH1 VST lease. |
| UX06: live inserts | Add/bypass/reorder/remove one/two VST3 per any existing channel | Native graph keeps editor/vendor-state integrity; safe graph handoff when supported, bounded transition; rejected plugin preserves prior audio. |
| UX07: create vocal Group | Multi-select channels → GROUP SELECTED | One independent Group ChannelId, selected **exclusive primary OUT** goes to Group→MAIN; existing parallel sends preserved; no double-dry / borrowed channel. |
| UX08: share reverb | Drop Reverb to SEND → NEW FX RETURN or SEND + | Creates independent FX return + parallel PRE/POST edge; original dry OUT remains. FX return has native fader, insert, meter and routable output. |
| UX09: monitor mix | SENDS mode → choose MON bus / Sends-on-Faders | Mix monitor independently to verified output pins; MAIN faders remain unchanged; continuous send gain adjustments never global MUTE. |
| UX10: choose destinations | MAIN / BUS OUT → verified output name | Valid physical/virtual routing only; output endpoint/clock reconfiguration is guarded when necessary, no automatic sound to a different speaker. |
| UX11: save/load Show | Save; Preview changes and mapped devices; Restore | Actual plugin state, graph/IDs/sends and I/O mapping round-trip or atomic reject; no silent unexpected ARM. |
| UX12: feedback/error | Try missing pin, cycle, bad VST, stale device, limit | Precise visible rejection, previous accepted live graph intact or disarmed only on lost hardware; retry is possible, no stuck pending. |

**One user action** means the interface owns validation, graph preparation, native ACK and safe re-arm where appropriate. It does **not** mean omitting safety checks. Routine fader, pan, mute, solo, send levels should be continuous RT parameters, not topology rebuilds. Structural software graph edits should not silence the full mix once online prepared-handoff is proven. Physical driver/clock changes are exceptional and may require a safe short disarm with clear confirmation.

## 5. Functional requirements

**FR-01 Universal channels.** Same core source selector, processing format, pan/balance, real FX, independent meter, named primary OUT, sends, fader, mute/solo and Show identity on CH1..CH-N, Group, FX Return (role-specific input section only). Capacity is CPU/memory/driver-budget admitted, not a promise of infinite channels. A hidden channel remains audible unless explicitly muted.

**FR-02 Source/format separation.** Format of SOURCE and processing CHANNEL are independent. MONO source → STEREO processing is allowed with deliberate pan projection. STEREO L/R → MONO uses one documented downmix rule. Source stereo pins can be nonadjacent distinct verified indices. Bus/FX destination format is independent. Processing format determines single/dual meter and effective pan/balance behavior.

**FR-03 VST3 parity.** No fake browser FX; all IDs resolve to genuine prepared hosted plugins. Preload silent channels, active bypass, drag/reorder, copy chain, native editor and validated save/restore. Define policy for incompatible mono/stereo plugin bus layouts and added plugin latency. Do not claim Master FX/PDC until exercised in production.

**FR-04 Independent mix roles.** Input, Group, FX Return, MONITOR, MAIN, optional Sub/Cue and control-only VCA are distinct. Group is serial primary output, FX Return is parallel sent wet audio, Monitor is an independently routed mix, VCA moves control parameters not PCM. Bus-to-bus directed routing must reject cycles. Main/output destinations are legal user choices, not OBS-only.

**FR-05 Send semantics.** Each send has an actual destination, enabled state, gain/pan, documented PRE/POST tap and channel conversion. Sending to new return/group is direct. On existing edges, level can change without graph rebuild. Sends-on-Faders uses an unambiguous active bus label and exit returns main faders exactly.

**FR-06 Safety and recovery.** Guard stale device generations, revision races, missing pins, plugin prepare errors, unavailable clocks, duplicate patch and CPU budget. Emergency MUTE always works. No silent success label from JS request, and no auto-unmute after hotplug or unsafe failure.

**FR-07 Performance and portability.** Audio callback allocates no heap, waits on no UI/OS lock and accesses no disk/window/driver enumeration. Faders and meters remain responsive on a standard Windows 11 PC without discrete GPU. No blur-heavy filters or hot-path DOM remount. Expose actual device, buffer, sample rate, plugin latency and supported routing; use saved stable logical I/O identity with explicit remap across hardware.

**FR-08 Control surfaces and scope.** Keyboard + mouse accessible; future MIDI/OSC/VCA only when native verified. No mandatory audio recording/DAW arranger for completion.

## 6. Acceptance gates and meaningfully-sized releases

**G0 — hardware truth (parallel release blocker):** identify #15 zero-FX crackle and direct monitor bypass, test physical ASIO and WASAPI, 30-minute dry run, fader/MASTER MUTE silence, audio device changes and actual clock/output path. No "Stable" based solely on CI.

**G1 — full-fidelity production graph convergence:** CH1–CH10, four AUX, native CH1 vendor editor/Orbit, existing CH2+ FX, MONITOR/MAIN/secondary, plugin state/Show all work on one identity-preserving prepared ChannelId graph and one callback. Correct dual meters/format. No dry-only replacement accepted.

**G2 — universal operator Console:** new CH11/CH-N, format flip, native FX on silent and patched channel, Group Selected, FX Return and same-channel sends, direct IN/OUT, adaptive banks and pinned MAIN. Complete continuous 2-mic→Group + separate reverb + Monitor audible journey before Done.

**G3 — online transaction quality:** no global mute for safe graph-only adds, sidechain/layout/PDC as admitted, true rollback, plugin tail and crossfade policy, Show/Scene/Recall Safe, CPU preflight. Reject or disclose unsafe online operations. Fader/send parameter changes never device restart.

**G4 — ship gate:** exact-head CTest/Windows packaging + Chromium across 1366/1440/1680 widths + low-end GPU audit + actual hardware FAT. Compare native PCM/meter/Show with golden fixtures and clearly versioned artifact provenance. Operator sign-off required for physical sound.

**NO RELEASE CLAIMS:** Browser-only CH11, decorative FX slots, one Group demo requiring empty FX/AUX/MON/Show, forced global MUTE for ordinary send gain, untested VST state loss, and no verified #15 audio.

## 7. Current verified implementation boundary (not PRD deliverables)

Audited **main cad535e67c764f471b01da5b04984bd3d97c47b6**. Live callback remains fixed **10 input + 4 AUX**, CH1 special when the old vendor chain is occupied. Offline ChannelId roster/PCM/publisher and restricted #263 Group pilot are not equivalent to a fully dynamic live engine. FAST3 one-APPLY exists, but many visible manual STAGE/MUTE labels and legacy paths remain. **Draft PR #265** proposes format/meter/native-source bank UI fixes but is **not merged** and must pass exact-head CI; no claim that CH11+ or full online bus additions have shipped. Factory FX lock is empty. Hardware issue [#15](https://github.com/masarray/arsonmix/issues/15) remains unqualified.

Issue epic: [#192](https://github.com/masarray/arsonmix/issues/192). Existing architecture constraints: [ADR R5](ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md); latest attested code ledger [reconciliation](RECONCILED_PROGRESS_ARSONMIX_20261101.md). Historical PRD v4.1 retained as audit evidence, superseded for UX decisions by this document.

## 8. MANDATORY V5.1 PRODUCT DELIVERY AMENDMENT — PUZZLE SOCKET V8

**Effective 2026-11-01.** This section is normative over the historical G0–G4 sequencing above; none of the existing product capabilities, visual quality or realtime safety requirements are reduced. The **implementation integration authority** is [Puzzle Socket v8](PUZZLE_INTEGRATION_CONTRACT_V8_20261101.md), with a machine-readable required-fields source in [contracts/console-puzzle-v8.json](../contracts/console-puzzle-v8.json). Product scope remains a realtime **LIVE DIGITAL MIXER** and VST3 host, *not* a DAW timeline/audio editor. Benchmark Studio One/Adobe Audition **Mixer/Console** and eMotion LV1 operator workflows by behavior, not copyrighted visuals.

### P8.1 One complete operator product, not three prototypes
The user will only be asked to test a **feature-complete integrated Windows mixer** after internal G1–G3 proof. No partial 'First Sound' EXE, synthetic GUI, separate ENGINE/FAT trial, or screenshot is customer UAT. An engineering rig is responsible for pre-UAT physical QA when available; an unavailable rig is honestly **PENDING**, not CLEAN/PASS.

A routine operator session must be possible from ONE compact Console: select ASIO or Windows Audio device and real enabled input/output endpoints in a compact drawer, then select **IN, processing M/ST, actual VST3 inserts/editor/bypass/reorder, PRE/POST SEND, primary OUT, PAN, MUTE, SOLO, fader and true native meter** on each accepted channel. Main is pinned. Normal source changes require one user action with native ACK; the big 10-row patch matrix, logical software-input alias wizard and manual STAGE→MUTE→APPLY→ARM are not mandatory. Advanced Patch remains available separately. GUI stays light on ordinary Windows/iGPU hardware and must not fake LIVE or plugin/port availability.

**Physical SOURCE != channel processing format.** On an interface with exactly one verified input pin the user may choose Mono IN1 and optionally stereo DSP processing through an admitted conversion, but never physical STEREO IN1/IN1. If the selected recording endpoint is VB-Cable CABLE Output, raw input is silent until a feeder application plays to CABLE Input; choosing Realtek playback alone cannot supply the cable capture. Distinguish device open, route ACK, raw capture PCM, processed channel PCM, MAIN PCM and output armed with actual callback telemetry.

### P8.2 User-visible acceptance = one complete musical flow
All features below must coexist in one app and one verified integrated SHA:
- CH07 can accept physical IN1 and CH02 can independently accept physical IN2 where the active driver reports both; no hardcoded CH1 and no phantom pins.
- Both can own **actual** VST3 inserts, bypass/reorder and open real editors without breaking DSP or losing vendor state.
- A real dynamic CH11 can be created; Group Selected is a **serial** primary output to MAIN without double-dry; wet FX Return is a **parallel** PRE/POST send path, not a renamed CH10 input.
- MAIN output and independent MONITOR/Cue physical/virtual endpoints can be chosen from compatible verified destinations; faders, mute, solo, pan and sends change native sound; Show save/reopen restores stable channel identities, plugin chunks and routing with safe remap/rejection.
- Structural graph/FX edit failures preserve previously accepted audio; routine continuous mixing does not require global MUTE/restarting device; reject unsuitable layouts and unstable drivers. Report actual latency, not an acoustic 0ms guarantee.
- Performance on standard Windows hardware, coherent native WebView assets, and physical 0FX/full-FX acoustic verification are release conditions; issue #15 remains a blocker until evidence exists.

### P8.3 Three puzzle owners, measurable delivery
**ENGINE #266** supplies one native callback + accepted ChannelId graph + real source/output pin/PCM/FX/Show authority. **GUI #271** consumes that exact graph and sends intents, never invents DSP/ports or its own accepted state. **INTEGRATION #270** owns the single cross-layer fixture/test, combined CI and packaging. PR #279 contains the executable socket preflight; it is a *contract check*, not sound certification.

Each owner must produce a **Puzzle Handoff** on [epic #192](https://github.com/masarray/arsonmix/issues/192): source SHA and changed files, one shared fixture version/hash, real operation supported by the installed callback or explicit NOT CONNECTED, tests at that SHA, cross-owner blocker and its corrective code change. A standalone green CI, offline Group or browser screenshot is not accepted as milestone completion.

**Exit sequence:** S0 same producer fixture + consumer + validator → S1 actual native callback one-input/direct-console complete sound path → S2 complete two-VST3/dynamic/Group/Return/Monitor/Show mixer → S3 one exact-SHA Windows/Chromium/package, independent hardware qualification or explicit PENDING → only then user UAT. Do not reduce scope or skip a failed gate for speed.


## 9. NORMATIVE V5.2 PRODUCT DESIGN LOCK — USER-APPROVED PR #278 MIXER WORKFLOW

**Approved 2026-11-01; precedence:** This §9 overrides any older GUI navigation/layout defaults in this PRD, v7, v8 handoffs or legacy M4 workbench. **The user explicitly selected [PR #278](https://github.com/masarray/arsonmix/pull/278), \`design-references/audition-mixer-study/index.html\` at its reviewed design-study baseline, as the correct visual and workflow direction.** This is the primary product design reference; the source remains isolated as an interactive browser study and **does not produce audio**. Adopt its **geometry, visual hierarchy, and fast interactions**, not its simulated audio, dummy device labels, fake plugin instances, or DOM full-rebuild implementation. No competing GUI concept or fresh redesign without explicit owner decision. [ARD v5.2 §K](ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md) specifies the engineering realization.

### P9.1. Product identity: DAW-grade mixer experience, NOT a DAW editor
ArSonMix is a **native realtime virtual digital mixer** (karaoke, stage, podcast, radio, livestream) with **Adobe Audition Multitrack MIXER as the primary single-window interaction/geometric reference** and Studio One/Fender Studio Pro Console as secondary **productivity** patterns (narrow strips, channel banks, multi-selection, copy/paste, groups, scenes and flexible plugin operations). eMotion LV1 informs live cue/sends/banking and operational reliability. No timeline, waveform editing, song sequencer, READ/WRITE/TOUCH automation lanes, MIDI arranger or DAW transport/record-arm workflow. In #278, remove \`auto-section\` as DAW automation; **input TRIM remains useful**. Do not copy copyrighted app assets or misleading OEM labels.

### P9.2. Exact operator homepage and strip anatomy
**On launch the main window IS the live Mixer Console**, not Audio Setup, signal-flow editor or an engineering workbench. One uninterrupted horizontal strip surface, compact title/action bar and a small collapsible section rail. Each strip aligns in consistent horizontal bands across channels (top→bottom):
1. **IN and OUT directly selectable on every eligible strip**, actual ASIO/WASAPI capture/render pins and native Bus roles; format M/ST is a separate adjacent processing-width choice where supported.
2. **VST3 insert rack:** ordered, real slots with enable/bypass, drag-reorder, click slot→search/add effect, double-click/open editor, safe add/remove; no detached FX-workspace prerequisite.
3. **SENDS:** actual available slots, destination, PRE/POST, enabled state and send level/pan; Sends-on-Faders for fast monitor/FX mixes. #278's 16-slot demonstration is a UX scalability reference, NOT permission to advertise 16 working sends when native has only four.
4. **EQ/trim and PAN:** compact EQ access to actual plugin/native EQ only, digital gain trim, pan center snap with a bipolar track filled from center toward L/R (not a left-origin fill).
5. **MUTE/SOLO/CUE controls**, native safety semantics and no recording/DAW automation toggles.
6. **FADER and real fixed-color L/R LED peak meters:** green→yellow→red thresholds stay in place; only segments reached by actual PCM light up, clipping latches appropriately. No simulated/animated random meter in production.
7. **Channel name, role/color and selection** at footer. Always **pin MASTER/MAIN** on right and keep a consistent visible signal path. Input, Bus, FX Return, Monitor and MAIN remain semantically distinct native roles; no CH10 relabel trick.

Horizontal banking, Narrow mode, filter by channel role/solo, collapsible rail, Ctrl/Shift multi-select, inline rename and Ctrl+C/Ctrl+V settings copy must be available as in #278. Copy/paste never silently reassigns physical I/O, driver ownership, or hardware device selection. Avoid excessive modal layering, 10-row patch wizard, mandatory Software Input alias creation, repeated STAGE→MUTE→APPLY→ARM and three separate FX/ROUTE/SENDS workspaces for common use. Expert matrix may exist under a deliberately optional **Advanced Routing** surface; device choice is ONE compact drawer.

### P9.3. The immediate live-audio interaction contract
User flow MUST be: launch → select real audio interface/device ONCE → choose \`IN1\` directly on CH07 (one gesture) → actual capture signal on native channel meter → fader/VST3/Send edits with immediate audible low-latency effect → choose legal output on MAIN/monitor. No full device stop/reopen or global mute for routine fader, pan, FX bypass or safe channel source changes; structural plugin/graph mutations may internally prepare/swap and ACK. UI does not show technical revision numbers as a user task. Playback/capture availability and PCM are distinct truth states; no fake stereo pin pair on a one-pin device; VB-Cable capture remains silent unless fed by the playback side of that cable. Latency is physically nonzero depending on driver/buffers/plugin chain; aim for minimum safe realtime roundtrip, don't claim literal zero ms.

### P9.4. Menu/feature semantics that cannot be conflated
- **\`LINK FADERS\`** (relative level + optional linked mute/solo across selected channels) is a control group. It is NOT a DSP summing Group Bus. \`ROUTE TO GROUP BUS\` creates/chooses a native **serial Group Bus**, with exactly one primary dry path onward to MAIN. Both controls must be named/behave distinctly.
- **\`FX RETURN\`** is a native parallel wet receive path; bus and send cycles rejected. Source sends PRE/POST with correct gain/pan, no doubled dry.
- **\`SCENE / SHOW\`** stores/restores real native accepted mixer state and VST3 vendor chunks; optional Scene Safe protects live targets. Demo state-only Scene Save is unacceptable.
- **\`PLUGIN MANAGER (F8)\`** searches native scanned/validated VST3 catalog with category, vendor, favorites/recent/hidden and real load/editor/bypass/reorder. Demo JSON/import and dummy plugin list are not production functionality; scanner/errors never block realtime audio callback.
- **\`CUE/PFL / MONITOR\`** requires real separate monitor DSP/output support before enabling, no demo-only illuminated buttons. No fake arming/record functions from a DAW.

### P9.5. Non-negotiable operator acceptance journeys
1. From ONE Console select device, direct \`CH07 IN1\` and MAIN output without modal setup workflow; on one-pin interface see \`IN1 MONO\` only.
2. Select \`CH02 IN2\` independently where real pin 2 exists; load/bypass/reorder two genuine VST3 effects while audio remains continuous and corresponding native meters respond.
3. Multi-select channels, **LINK FADERS** with relative changes; separately create **Group Bus**, route selected primary outputs to bus→MAIN and verify no duplicate dry. Add true FX Return and POST/PRE sends in parallel.
4. Find/install/select plugin from native Plugin Manager, open actual vendor editor; switch narrow/filters/banks without losing pinned MASTER or mutating DSP.
5. Save/recall actual Scene/Show with stable ChannelIds; use Cue/PFL/Monitor if backend supports real legal endpoint; input/device fault shows clear no-PCM status, not false LIVE.
6. Verify all controls in actual packaged Windows WebView2 build (not just the #278 HTML demo), 1024×600, 1366×768, 1680×900 and standard iGPU; no heavy blur, glow, hover-triggered full DOM rerender, jitter or excessive large fonts/cards.
7. FAT validates one exact combined engine+GUI+test SHA and independent hardware evidence when available; if not, mark acoustic QA PENDING without passing certification. User UAT only after feature-complete integrated candidate.

**Priority enforcement:** When UX conflicts arise, this user-approved #278 workflow wins for operator interaction. ARD RT/thread safety, physical truth, plugin lease ownership, ChannelId semantics, accepted native ACK, and sound integrity remain inviolable. ENGINE #266 serves the controls; GUI #271 implements the production screen in \`web/**\`; INTEGRATION #270 tests the operator journey. No parallel standalone UI as another release product.
