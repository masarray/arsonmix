# ArSonMix PRD v5.0 — Operator-First Live Mixer Console
Date: 2026-11-01 | Status: **NORMATIVE PRODUCT TARGET, NOT SHIPPED FEATURE ATTESTATION** | Supersedes the workflow and priorities in PRD v4.1. Companion: [ARD v5.0](ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md).

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
