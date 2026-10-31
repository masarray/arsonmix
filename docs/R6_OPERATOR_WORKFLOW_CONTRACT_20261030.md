# R6 — Professional Console GUI / Workflow Acceptance Contract

**CROSS-THREAD RECONCILIATION — 2026-10-30:** [New canonical high-level MS0–MS7 product roadmap](ROADMAP_LV1_STUDIO_PRO_MIXER_PARITY_20261030.md), merged at main@d8d70a6, owns milestone numbering and benchmark facts including current **Fender Studio Pro 8.2** naming. This R6 document is a lower-level native architecture/UX acceptance companion, **not a competing product roadmap**. Read roadmap MS0 (#15 hardware), MS1 (#173), MS2 (#174), MS3 (#175), MS4 (#95/IN-FLIGHT G5A draft PR #178), MS5 (#179 Channel Focus/Sends-on-Faders), MS6 (#180/#85 Show and portability), MS7 release hardening. UX PATCH/FX truth is [#182](https://github.com/masarray/arsonmix/issues/182), complementary to #179; preserve L2 #176. PR #178 is DRAFT and unmerged; its ten changed files include LiveChannelFxGraph.h and web/bridge.js/plugin-ui.js and must be reconciled by exact HEAD before any GUI or audio graph edit. The earlier narrower R5 shorthand MS4+portability below is historical; **MS6 owns portable Shows in current product milestone numbering**. Hardware quality remains unqualified even if CI is green.

Date 2026-10-30. Companion: R6_CURRENT_STATE_ARCHITECTURE_EXECUTION_20261030.md. Design requirements do not imply engine delivered.

## Fixed shell inherited from approved #176
Top segmented MIXER | FX RACK | PATCH. Left rail FX | ROUTE | SENDS and utility panels. All CH1–10 + MASTER share one L2 seven-track grid with exact fader/meter alignment. Upper bank height changes synchronously, never independently for MASTER. Do not add a toolbar above mixer, enormous cards, glow/blur or new audio-state controllers. Use existing BTN-180, calibrated meters and Fader PNG materials.

## Channel Focus workflow
Selecting Lead Vocal should reveal (1) Physical source MIC1 MONO, distinct from (2) Channel processing format MONO or STEREO, (3) native VST3 insert list/bypass, (4) four AUX sends PRE/POST/gain/pan, (5) logical MAIN/GROUP route, (6) actual physical output readback, and (7) native DRAFT/ACK state. The selected inspector may be overlay/dock in shared workspace; must not shift fader coordinates. Changing channel format to STEREO with mono MIC1 must not create phantom R pin. Stereo source -> mono processing must show declared downmix policy.
On CH2–10 show only actually supported VST3 slots. Master FX placeholder requires clear NOT ROUTED indication rather than apparent active ADD. FX/RACK route editing must not destroy CH1 plugin instance/Orbit.

## BUS + Sends workflow
A named AUX1 Reverb is ONE shared stereo FX return with real native signal and meter/fader. Selecting AUX1 shows source channels' PRE/POST send levels and correct mono/stereo pan from a single focused grid. FX AUX is internal processing, not a physical headphone output. GROUP, MONITOR, MATRIX, DCA are distinct types and must not appear LIVE before their native engines exist. Sends-on-Faders (future) shows a highly visible selected bus/mode; it cannot silently control MAIN faders.

## PATCH workflow — PATCH2 verified-benchmark revision (2026-10-31)
The former PATCH1 matrix-first interaction is a delivered baseline, NOT the target daily workflow. The [PATCH2 PRD](PRD_HARDWARE_AUDIO_IO_MATRIX_20261030.md) and [PATCH2 ARD](ARD_HARDWARE_AUDIO_IO_MATRIX_20261030.md) supersede its operator guidance. **Waves LV1** offers direct source selection on the selected channel and global filtered patching. **Fender Studio Pro** defines reusable named software I/O channel aliases in Audio Setup and selects those from the console channel. Source picker must be discoverable on the mixer without opening PATCH.

- **Ordinary use, after device setup:** CH03 SOURCE → MAONO Input 1 (Mono); CH06 SOURCE → MAONO driver-verified Virtual L/R (Stereo). Target ≤2 actions, ACK-backed, no routine per-row STAGE or manual global MUTE/APPLY; NOT IMPLEMENTED until the native live-safe single-input transaction and issue #15 physical FAT pass. Currently retain PATCH1 MUTE→APPLY→ACK honestly and do not claim seamless switching.
- **AUDIO SETUP:** driver, rate, buffer, actual sparse hardware pins; configure and name reusable mono/stereo logical sources once; stable device-specific profile/Show remap. No fabricated virtual channels or implicit aggregation of independent Windows capture endpoints.
- **ADVANCED PATCH:** INPUT = verified physical/logical source → channel with clear signal direction; OUTPUT = real MAIN/MONITOR/OUTPUT1 capabilities only; INTERNAL = supported sends/buses only. One shared routing controller, accepted vs pending status and rollback; no fake group/DCA/device-to-device grid.
- **Audio safety:** native source-only quick-switch must preflight epoch/revision, compile immutable prepared graph off audio thread, handoff safely at block boundary, avoid discontinuity/crackle, preserve plugin/aux state and existing operator mute. Structural and unsafe changes still require explicit guarded APPLY and no auto-unmute. Reject without losing previous audio.

## Operator acceptance journeys
A) MIC1 MONO source → STEREO-format Lead Vocal while same physical pin selected; correct native sample result, no phantom R and no fader shift.
B) Music L/R stereo → MONO channel; only once 0.5(L+R), anti-phase explicit and native state reversible.
C) MIC1 + MIC2 sends POST with independent levels to ONE stereo AUX1 reverb instance; audible actual return plus meter/fader/ACK.
D) Typed GROUP and MON buses give independent signal only when implemented; no double-summed MAIN or direct-out feedback.
E) 8-output ASIO interface maps MAIN to 1/2 and MONITOR to 3/4 with separate test tones; 2-output card rejects 3/4.
F) 20 repeated FX/ROUTE/SENDS swaps, Mono/Stereo format flips, renderMixer and window resizes; all 11 faders remain aligned at 1680/1440/1366/1024 widths; no plugin state loss or pointer stutter on standard iGPU.
G) Portable user-owned Show reopens routing/plugin state and displays explicit device remapping if port unavailable.
H) 0FX ASIO/WASAPI real-device recording FAT must clear #15 before production certification.
