# R6 — Professional Console GUI / Workflow Acceptance Contract
Date 2026-10-30. Companion: R6_CURRENT_STATE_ARCHITECTURE_EXECUTION_20261030.md. Design requirements do not imply engine delivered.

## Fixed shell inherited from approved #176
Top segmented MIXER | FX RACK | PATCH. Left rail FX | ROUTE | SENDS and utility panels. All CH1–10 + MASTER share one L2 seven-track grid with exact fader/meter alignment. Upper bank height changes synchronously, never independently for MASTER. Do not add a toolbar above mixer, enormous cards, glow/blur or new audio-state controllers. Use existing BTN-180, calibrated meters and Fader PNG materials.

## Channel Focus workflow
Selecting Lead Vocal should reveal (1) Physical source MIC1 MONO, distinct from (2) Channel processing format MONO or STEREO, (3) native VST3 insert list/bypass, (4) four AUX sends PRE/POST/gain/pan, (5) logical MAIN/GROUP route, (6) actual physical output readback, and (7) native DRAFT/ACK state. The selected inspector may be overlay/dock in shared workspace; must not shift fader coordinates. Changing channel format to STEREO with mono MIC1 must not create phantom R pin. Stereo source -> mono processing must show declared downmix policy.
On CH2–10 show only actually supported VST3 slots. Master FX placeholder requires clear NOT ROUTED indication rather than apparent active ADD. FX/RACK route editing must not destroy CH1 plugin instance/Orbit.

## BUS + Sends workflow
A named AUX1 Reverb is ONE shared stereo FX return with real native signal and meter/fader. Selecting AUX1 shows source channels' PRE/POST send levels and correct mono/stereo pan from a single focused grid. FX AUX is internal processing, not a physical headphone output. GROUP, MONITOR, MATRIX, DCA are distinct types and must not appear LIVE before their native engines exist. Sends-on-Faders (future) shows a highly visible selected bus/mode; it cannot silently control MAIN faders.

## PATCH workflow
INPUT: logical channel/source row against only enabled physical input columns. Add/rename mono/stereo logical sources in a native-backed later milestone; nonadjacent stereo physical pins are valid if enabled. Two-input device = two columns by default.
INTERNAL: source→bus routing with selected-edge highlighting, independent tap state and cycle/duplicate rejection; no entire web of lines every frame.
OUTPUT: named logical bus and independently verified physical render pin. MAIN L/R is currently read-only. With native MS3, STEREO bus→two distinct ports or MONO bus→one port supported. 2-output device rejects MONITOR 3/4.
Every structural change stages DRAFT, requires MUTED APPLY, waits for native ACK; no auto-unmute or misleading LIVE state. Invalid plugin layout, stale device, missing ports and audio feedback must yield concise operator language and no lost accepted state.

## Operator acceptance journeys
A) MIC1 MONO source → STEREO-format Lead Vocal while same physical pin selected; correct native sample result, no phantom R and no fader shift.
B) Music L/R stereo → MONO channel; only once 0.5(L+R), anti-phase explicit and native state reversible.
C) MIC1 + MIC2 sends POST with independent levels to ONE stereo AUX1 reverb instance; audible actual return plus meter/fader/ACK.
D) Typed GROUP and MON buses give independent signal only when implemented; no double-summed MAIN or direct-out feedback.
E) 8-output ASIO interface maps MAIN to 1/2 and MONITOR to 3/4 with separate test tones; 2-output card rejects 3/4.
F) 20 repeated FX/ROUTE/SENDS swaps, Mono/Stereo format flips, renderMixer and window resizes; all 11 faders remain aligned at 1680/1440/1366/1024 widths; no plugin state loss or pointer stutter on standard iGPU.
G) Portable user-owned Show reopens routing/plugin state and displays explicit device remapping if port unavailable.
H) 0FX ASIO/WASAPI real-device recording FAT must clear #15 before production certification.
