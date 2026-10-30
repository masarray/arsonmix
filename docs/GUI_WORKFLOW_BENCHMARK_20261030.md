# ArSonMix — Canonical GUI Workflow Benchmark: eMotion LV1 Classic/v17, Studio One, Focused Routing
Date: 2026-10-30 | Status: PRODUCT / UX DESIGN CONTRACT (not an implementation claim)
Companions: PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md; ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md; CUSTOMER_VOICE_REQUIREMENTS_20261030.md.
Source baseline updated: main@527e155 (#166 and #167 merged). FX/ROUTE/SENDS and custom INPUTS/OUTPUTS/DEVICE GUI are present, but custom driver/rate/buffer setup, independent physical output patch, G3 inline sends and real hardware FAT remain unfinished. Preserve latest approved ArSonMix GUI assets, web/styles-src/20-button-180.css, FX Rack and native graph.

## R5 channel-format workflow benchmark (must be reflected in native behavior)
See [ADR R5](ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md). In MIX, one compact M/L·R badge on each real selected channel changes its *processing format*, independent of physical source selector; selecting INPUT Mono/Stereo is not channel-format selection. CHANNEL Focus shows source (physical Mono L or Stereo L/R), processing format, FX layout, sends/bus and physical output separately. PATCH INPUT matrix presents only active real device pins, independent L and R choices and a clear conversion summary; it must not force adjacent pair or show blank columns on 2-input hardware. Structural flip stages DRAFT, native positive ACK makes LIVE, invalid plugin/port rejects; when changing mono/stereo the fader, MUTE/SOLO, FX rack/Orbit and selected-bank geometry remain stationary. eMotion LV1 Flip Mono/Stereo and Studio One Mono/Stereo software I/O are workflows to emulate, not icons/artwork to clone. Different mono-source/stereo-channel, stereo-source/mono-channel, mono/stereo bus and output are separate validated operations; don't claim unsupported output buses LIVE.

## A. Design purpose and non-copying rule
ArSonMix is a Windows-first, independent live digital mixer and VST host. Model the *operator mental models* of live consoles and DAWs without copying third-party logos, imagery, sprites, color tokens, copyrighted pixels or proprietary application code. The user supplied nine reference screenshots in chat; filenames below identify the evidence only. These third-party screenshots are NOT bundled into this repository.

Borrow three orthogonal ideas:
- **Waves eMotion LV1 / LV1 Classic:** persistent fader/meter strip with switchable upper information/processing mode; color-coded banks; selected-channel focus; ALL overview differentiates inputs, group, FX AUX, monitor, DCA and master; Show/Patch/Setup are distinct and coherent.
- **PreSonus Studio One:** editable logical MONO/STEREO inputs/outputs mapped by M/L/R cells to actual device pins; track/FX/bus strip routing; inserts versus sends are separate; bus outputs not synonymous with physical output indices.
- **FL Studio selected source/destination illustration:** highlight a focused audio edge on demand, showing source and destination, never an animated spaghetti diagram for all connections.

## B. Screenshot-by-screenshot observation -> ArSonMix contract
| Evidence file supplied by user | Visible UI behavior and hierarchy | ArSonMix translation | Scope |
|---|---|---|---|
| 5.jpg | LV1 Classic physical control surface: 16 small channel labels, colored bank keys, SOLO/MUTE, encoders, fixed faders, right bank controls | Stable selected-channel identity and compact 16-channel *bank layout concept* (not 16 actual native channels yet); keyboard/controller mapping later | UX now / hardware controller later |
| emotion-lv1-live-mixer-2.jpg | LV1 Mixer 1, 16 color strips, 8 inserts shown per strip, mode rail, faders/meter/masters stay in place | MIX upper fixed-height mode area; 6 rows visible in compact ArSonMix strip, eight live slots only where native supports them; no fake multi-insert in CH2–10 | UX G1/G2; native expansion G5 |
| emotion-lv1-live-mixer-v17.jpg | Channel focus shows input/preamp, rack, filter/EQ/dynamics, send/output grids; lower strips show alternate DYN/EQ mode | CHANNEL inspector selected CH above persistent MIX; genuine plugin editor only, never invent built-in EQ/dynamics; one highlighted focus, reversible without graph reset | G1/G3 and later native DSP |
| emotion-lv1-live-mixer-4.jpg | ALL window separates input 1–80, group, FX, monitor, links, DCA, master and matrix into regions with mini meters | Future ALL/OVERVIEW scalable banked compact strips, separate typed bus families, capability-accurate counts; never claim 80 channels exist | G4+ UX / typed bus model |
| image(20261029-220147).png | Studio One Audio I/O setup: logical named mono/stereo inputs mapped M / L / R to physical device columns | Custom INPUT PATCH matrix, Add Mono/Stereo, rename logical sources, validate enabled port mask and device epoch | G1/G2 |
| image(20261029-220219).png | Studio One dark I/O grid: 8 mono logical inputs and stereo pairs 1/2, 3/4, etc | One physical port may be represented by a mono logical input and an explicit stereo pair without confusing physical identity; device report owns port enumeration | G2 |
| image(20261029-220352).png | Studio One example: multiple channel sends into shared Bus 1/2/3; destination processing once | True FX send/return inspector, level / PRE/POST / PAN, shared AUX reverb, FX return fader; do not instantiate one reverb per source | G3 |
| image(20261029-220439).png | Studio One console overview: some tracks routed into intermediate buses, then MAIN; colored arrows clarify destinations | Directed bus graph; destination badge under strips; no direct-to-MAIN duplicate when routed only through GROUP; source/destination highlight on selection | G4 |
| image(20261029-220527).png | FL source selected and destination selected, one visual wire focus | Selection-dependent trace with explicit source/tap/destination and accepted live graph state; no per-frame draw/animation | G3/G4 |

These are descriptions of supplied images and **design targets**. They do not prove ArSonMix currently implements the illustrated facilities. Some LV1 screenshots show historical UI while eMotion LV1 v17 has newer customizable layouts; infer durable interactions, not exact screenshot-era visual controls.

## C. Navigation and surface anatomy — one session, different presentations
Persistent top bar: MIX | CHANNEL | FX RACK | ROUTING | SHOW | SETUP. One compact STATUS strip: REAL device/backend; physical in/out count; sample rate/buffer; OUTPUT MUTED/ARMED; LIVE/UNPATCHED/DRAFT/REJECTED. Keep diagnostics/plugin manager in utilities, not as modal obstacles.

MIX contains a fixed lower strip geometry: COLOR/NAME, PAN, MUTE/SOLO, live METER, physical FADER. Only an **upper fixed-height slot** is mode-selectable:
- FX: truthfully render actual live insert chain; CH1 existing native 8 slots (6 compact visible) and Orbit; CH2–10/AUX current one routed insert. Reserved extra rows disabled, not fake ADD.
- ROUTE: selected logical input + mono/stereo + actual source pin; logical output BUS and, separately, hardware output mapping where available. Navigation or view change never commits audio.
- SENDS: summary of each sender's AUX / GROUP destinations with send level, tap PRE/POST, pan, enable and LIVE/DRAFT indication. Details and edits use one selected channel inspector; route commit still native.
Switching FX/ROUTE/SENDS must not move faders, meters or mute controls; scroll position/selection survives. Do not destroy/remount working CH1 VST3 editors or replace FX RACK with a routing launcher.

CHANNEL view = selected strip within the context of the mixer. Draw INPUT -> INSERT FX -> SENDS -> BUS/OUTPUT in reading order; explain selected actual path, not a fake all-in-one EQ. One visual control state comes from native ACK; user can expand the vendor editor, but do not recreate vendor processors on navigation.

FX RACK is an independent workspace (not the ROUTING screen). Plugin manager, picker, favorites, real editor, Bypass/Move/Delete/Orbit stay bound to the same native instances. Preserve #166 CH1 ADD repair. Every native FX action reports pending, accepted, rejected with no optimistic fake LIVE state.

ROUTING advanced view = INPUT PATCH | INTERNAL SENDS | OUTPUT PATCH. Use source rows and real physical port columns for hardware; matrix cell state OFF / DRAFT / LIVE / REMOVED; 10x4 existing AUX matrix is separate from physical output. Horizontal paging for physical columns, bounded display; selected edge trace only. OUTPUT remains READ ONLY for MAIN L/R until true prepared native multi-output plan exists.

SETUP > AUDIO I/O = custom ArSonMix DEVICE | INPUTS | OUTPUTS with exact JUCE-reported backend, device, rate, buffer, active physical masks. Studio One-like Add Mono/Add Stereo creates logical labels, not physical channels; selecting L/R checks valid distinct pins and stereo layout. Retain raw JUCE fallback only as ADVANCED until custom device configuration is truly native.

ALL / OVERVIEW and SENDS-ON-FADERS are prospective extensions. ALL must be a performant virtualized/aggregated view of real owned bus types, not hard-coded 80 decorative strips. SENDS-ON-FADERS, when enabled, must conspicuously show the destination, offer single-action EXIT and preserve original main-fader values so an operator cannot accidentally change FOH level when adjusting monitor sends.

## D. Design system, responsive layout, visual/render budget
- Preserve ArSonMix graphite/metal aesthetic, channel colors, approved fader grooves, fixed LED meter thresholds and btn-180 tactile actions; standardize size/typography. Avoid heavy/bulky cards, excessive bold text, giant labels, complicated gradients.
- Compact 11–13px readable operator typography where feasible; button hit areas about 30–36px; scroll panels rather than shrink critical faders. These are proposals subject to real 1366×768/1440/1680 screenshot tests, not untested hard requirements.
- Signal/color meaning must be consistent: muted/not armed distinct from ON; FX bypass distinct from routing disconnected; color is never sole LIVE/DRAFT evidence. Use concise labels and accessible titles/keyboard.
- No CSS blur/backdrop-filter/fake glow, no pointermove DOM mutation, no full-rack HTML rebuild from periodic telemetry, no per-frame canvas wire refresh. Dirty-region/native meter updates with stale-event guard. Keep WebView2 page resident across startup/minimize/restore; no white flicker.
- At 1366×768 prioritize readable strip controls and global select; horizontal bank/page rather than squeezing columns below usable width. Do not confuse "16 visible channels in LV1" with a requirement to fit 16 ArSonMix channels regardless of screen width.
- Selection and active input mapping are state projections. One set of native graph+epoch+revision data controls MIX, CHANNEL, FX RACK, ROUTING and SETUP. No duplicate JavaScript side-graph that can diverge from sound.

## E. Primary user journeys / interaction invariants
J1 — 2 input / 2 output: SETUP picks verified ASIO/WASAPI endpoint; INPUT M/L/R patches MIC1/MUSIC; MIX CH1 input named; add CH1 real plugin; SEND POST to AUX1 REVERB; AUX1 return to MAIN; OUTPUT readback HW1/2; safe MUTE -> DRAFT -> APPLY -> ACK -> explicit UNMUTE. No phantom OUT3/4.
J2 — 8x8 hardware (G4 only): MAIN -> physical 1/2, MONITOR -> 3/4, SUB/other processed bus -> 5/6, REC/CUE -> 7/8; all audio independent; exact port ownership, no accidental MAIN duplication, safe reject on fewer outputs.
J3 — FX usability: MIX FX -> ROUTE -> SENDS -> CHANNEL -> FX RACK 20x with loaded CH1 2 VST3 including Orbit, without plugin recreation or geometry flicker. CH2 routed insert remains truthful one-slot until G5.
J4 — Shared FX: MIC1 POST -12dB and MIC2 POST -18dB (sample user levels, not program defaults) -> single AUX1 stereo VST3 reverb; one processor instance; solo/mute/fader PRE/POST policies tested for both sources.
J5 — Field failure: disconnected port, hotplug/driver change, stale epoch, VST preparation fault, 0-output device, feedback cycle or Show failure must yield explicit REJECTED; accepted audio topology unchanged; output stays muted where policy requires.

## F. Engineering ownership / contract tests
- web/index.html + web/app.js: permanent main MIX layout, mode bank and selected-strip state. If G1/G2 #167 merges, extend its own mixer-io.js/css instead of rewriting from older snapshots.
- web/bridge.js, web/plugin-ui.js: FX rack, CH1 existing multi-VST + Orbit, plugin picker, persistence. Never replace this working presenter with +ROUTE/FX.
- web/monitor.js: physical/native patch and AUX sends, DRAFT/ACK, output readback; do not build a second graph.
- src/Main.cpp and device registry: authoritative physical pin/rate/buffer enumeration and action dispatch; no fabricated hardware labels.
- engine/core/PreparedMixerBlock.h, ChannelSendPlan.h, AuxBusPlan.h, LiveChannelFxGraph.h and src/LiveDryRoutingLab.h: native routing and renderer. New typed output bus/pins must extend one deterministic graph and callback, prepared off realtime thread.
- Tests: screenshot diff/geometry at 1366/1440/1680; 20x UI mode switching; native ACK/reject; 2x2 and sparse 8x8 port data; selected wire focus; stress on integrated GPU under real meter telemetry; physical audio FAT remains separate.

## G. Product boundaries and reference documentation
- Existing native status on reviewed main: 10 input + 4 stereo AUX FX returns; at most 32 combined directed sends; CH1 true multi-insert VST3, other routed lanes one VST3 each; MAIN L/R physical output display is READ ONLY; no full GROUP/DCA/MONITOR/MATRIX/PDC/physical extra outputs; hardware 0-FX crackle issue #15 unresolved.
- #167 G1/G2 merged at main@527e155; preserve its FX/ROUTE/SENDS and INPUTS/OUTPUTS/DEVICE GUI. Extra physical outputs, full custom device setup and real-device FAT remain unfinished. Never overwrite newer source with old snapshots.
- Official LV1 v17 interface/mode/ALL documentation: https://www.waves.com/emotion-lv1-v17-news ; https://www.waves.com/mixers-racks/emotion-lv1/features
- LV1 Mixer Mode tutorial: https://www.waves.com/emotion-lv1-tutorial-4-1-mixer-window-overview
- LV1 Channel overview: https://www.waves.com/emotion-lv1-tutorial-3-1-channel-window-overview
- LV1 Patch: https://www.waves.com/emotion-lv1-tutorial-2-1-patch-window-overview
- PreSonus Studio One Pro 7 sub-out and feedback constraints: https://support.presonus.com/hc/en-us/articles/29973763309453-Studio-One-Pro-7-How-do-I-record-the-mix-back-to-an-audio-track-like-in-Pro-Tools
- Studio One Audio I/O routing tutorial: https://www.soundonsound.com/techniques/studio-one-audio-io-routing
- Existing canonical product contract and existing FX/Routing ownership constraints prevail when older images conflict with real runtime.
