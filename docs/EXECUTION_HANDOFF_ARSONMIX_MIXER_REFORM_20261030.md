# ARSONMIX — 2026-10-30 NEXT-THREAD EXECUTION HANDOFF (CANONICAL)

**R6 override (2026-10-30):** This historical G1/G2 handoff is NOT the current work queue. [Audited R6 architecture/current status](R6_CURRENT_STATE_ARCHITECTURE_EXECUTION_20261030.md), [operator UX contract](R6_OPERATOR_WORKFLOW_CONTRACT_20261030.md) and #176 L2 shell supersede it. Main@8251bd6e includes G1/G2, G3 and L2; next milestone #173 native mono/stereo, then #174 typed FX/group buses, #175 multi-hardware outputs, #95 FX/PDC and parallel #15. Keep all 11 faders linked, original VST CH1 Orbit, native ACK/MUTE, and existing left rail. Treat old statements below as historical for their date.


**This is the starting point for a new implementation thread.** Read in this order:
1. [PRD v2.0 — operator UX and bus requirements](PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md)
2. [ARD v2.0 — graph, I/O, safety, data model, milestones, tests](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md)
3. [FX Rack / Routing ownership](FX_RACK_ROUTING_CROSS_THREAD_CONTRACT_20261030.md)
4. [Native Smart Audio ARD](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), [baseline FAT](HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md) and current source code.

## New canonical sequencing note — ADR R5 overrides stale G1/G2 next-step text
Before any further channel format, bus, hardware-output or FX layout work, read [ADR R5 Mono/Stereo](ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md) and [R5 execution handoff](EXECUTION_HANDOFF_MONO_STEREO_REFORM_20261030.md). This document is a HISTORICAL earlier G1/G2 implementation handoff: G1/G2 #167 and G3 native AUX editor were merged by main@890cc310. Do not repeat its "next milestone G1+G2" or replace the new mixer-io.js. Current next primary vertical slice is MS1 native/GUI independent mono/stereo channel processing with legacy codec and tests; then MS2 typed buses/send conversion, MS3 real mono/stereo physical outputs via #143, MS4 FX/PDC/Show. #15 zero-FX FAT remains separate blocking qualification. Read latest main SHA before any edit.

## 1. Why this reform is necessary
The user showed 9 digital-mixer/DAW reference screenshots: eMotion LV1 live mixer and FX/ROUTE mode banks, Studio One custom hardware input-output matrix, DAW channel sends to Bus1/Bus2/Bus3, and selected source-to-destination FL-style wires. They want **a usable mixer**: INPUT and OUTPUT on the main window, all channel VST insert racks preserved, one-click FX/ROUTE/SENDS switching, real mono/stereo hardware pin maps and true AUX bus routing. Previous R3 styling optimized visual patch cells but failed the essential workflow when switching to routing replaced the working FX Rack.

**Never repeat the original error:** preserve CH1 multi-VST graph and controls even while native 10CH routing is running. No giant routing overlay as a substitute for a daily channel workflow. No raw JUCE Audio Setup as the desired product GUI—JUCE audio backend can remain.

## 2. Confirm latest current status before coding
At handoff authoring, repository was observed at **main@3b89c86** before this documentation change. The last code milestone at that SHA is titled "P1: restore FX Rack, portable VST favorites and WebView2 startup/restore residency"; exact-head Chromium on that SHA **SUCCESS**, Windows Native CI was still running. Source has an independent FX presenter in web/bridge.js and bridges for routed VST3 draft/ACK in web/monitor.js; despite this, there is **no implemented full custom physical INPUTS/OUTPUTS GUI**, no enabled arbitrary physical output patch, no complete MIX FX/ROUTE/SENDS bank, and no multi-insert CH2–10. Verify actual latest HEAD and do not merge the stale R4 draft branch on top of newer code blindly.

Current baseline engine: 10 input lanes + 4 dedicated stereo AUX FX returns, at most 32 PRE/POST sends, verified DIRECT physical input mask, native epoch/revision ACK, CH1 legacy eight-slot vendor chain processed in the same audio render callback (processLegacyChannelOne), one real routed VST3 per other lane/AUX. MAIN L/R only is physically exposed; output matrix remains read-only. Portable Show DRY save/browse/recall is real; full VST3 session recall not finished. Hardware crackle issue #15 is NOT qualified.

## 3. Next milestone to implement — a complete G1+G2 user-facing slice
**Do not submit mere color tweaks or isolated buttons.** Deliver one coherent mixer UX, native hardware I/O and clear multi-workspace navigation. If C++ physical output patch is not yet safe, show truthful MAIN L/R readback and disabled advanced OUT instead of fake checkboxes.

Functional tasks in dependency order:
1. **Schema / inspector:** introduce a single frontend I/O view-model for actual enabled device ports, route channels, bus/send and output. Reuse native arMixStatus.dryRoutingLab and acceptedRoute; keep drafts distinct. Do NOT introduce a second live routing engine or third audio callback.
2. **MIX control bank:** upper part of each channel can show FX/ROUTE/SENDS; fader, meter, mute, solo, identity remain stationary. Global mode toggle vs local focused channel. FX bank preserves six slot geometry and actual CH1 eight-slot rack. Per-channel route summary includes actual physical source or UNPATCHED; output summary MAIN L/R/valid physical endpoint.
3. **Custom AUDIO I/O SETUP:** in GUI show discovered ASIO/WASAPI device types and mask, inputs M/L/R matrix and output tab MAIN physical assignment current or unavailable. Use existing JUCE AudioDeviceManager in backend; do not write a custom driver to replace an ugly dialog. Hide unsupported driver features. Preserve advanced fallback.
4. **PATCH workspace:** advanced 10×port input matrix, 10×4 AUX sends and output readback remain, but may be accessed independently and selected channel from MIX. All edits route through existing native APPLY and ACK. Custom FX Rack and ROUTING must be separately navigable without rebuilding unrelated panel.
5. **Regression + artifact:** use Playwright at 1366, 1440 and 1680; real VST3 CH1 rack order/editor/bypass/Orbit across view switches; simulated 2-in/2-out and sparse 8-in/8-out device scenarios; same-epoch ACK/negative ACK and stale restart tests; GUI byte-parity and compiled Windows native artifact with provenance (if native files touched). Screenshot before/after must be judged for operator clarity. Provide user an actual Windows ZIP for hardware FAT.

Next slice **G3** makes existing four AUX FX returns genuinely usable via MIX SENDS view and focused wire trace: CH1 PRE/POST→AUX1 reverb, CH2 dry MAIN; fader/pre/post correctness, safe mute/solo and DAG, native ACK. Do not accidentally conflate this with G4 physical AUX output patch.

## 4. Real source ownership / no concurrent overwrite
- web/index.html + web/app.js = navigation, strip views, selected channel, static mixer geometry
- web/bridge.js + web/plugin-ui.js / .css = real VST3 Rack, picker, editor, order, bypass, portable favorites
- web/monitor.js = advanced native device patch, AUX sends, output readback, MUTE/ACK, routed VST3 draft bridge
- web/styles-src/* and generated web/styles.css = shared BTN-180 and correct CSS build
- engine/core/NativeDryRouteCommand.h, ChannelSendPlan.h, AuxBusPlan.h, PreparedMixerBlock.h, LiveChannelFxGraph.h, RealtimeRoutePublisher.h = validated core
- src/LiveDryRoutingLab.h + LiveAudioEngine.h = native multi-channel renderer and graph lease/epoch
- src/Main.cpp = JUCE transport setup, native action parser, safe GUI-to-engine command dispatch
- tests/live_routing_ui_contract_test.mjs, tests/ui_smoke.py, native_dry_route_command_test.cpp, dedicated_aux_bus_test.cpp, realtime_ten_channel_controls_test.cpp = extend, not delete.

If other threads touch the same ownership files, take exact latest SHA and reconcile their changes; do not overwrite with old baselines. Current open PRs such as #128/#117/#115 are historically based; do not bulk merge. Verify current outstanding issues in GitHub, not only this date.

## 5. Correct operational contracts / anti-naive design
- **State:** UI IDLE → DRAFT → PENDING → ACK LIVE or REJECTED. All structural actions MUTE-gated. On rejected/timeout/stale device, never draw a new LIVE connection; preserve old audio graph.
- **Physical hardware:** only active enabled input/output pins from real device; no fake output pair. Distinguish bus MAIN L/R from physical HW 1/2; for a 2-out card, AUX physical out not supported. No auto-unmute on any APPLY/recall.
- **Graph:** PRE = post-FX pre-fader; POST = post-fader; one process per source and FX-return per block, no BUS feedback, bounded 32 sends, channel direct-to-main explicit; do not add duplicate master sums.
- **FX:** CH1 actual multi-insert is never replaced by a route launcher. CH2–10 currently one routed instance; extra placeholders visibly disabled. Future >1 insert requires prepared graph ownership, zero RT allocations, PDC.
- **UI:** BTN-180 on actions; flat patch indicators; no CSS blur/glow or pointer-move animations; no eager catalog rebuild or per-telemetry full innerHTML.
- **No user trials of unfinished 5-line patches:** batch meaningful integrated milestone, run targeted local checks then one exact-head CI per coherent PR; Windows full build once per release cut, not on every docs/tiny change.
- **FAT:** 0 FX 48/44.1 kHz, 2 and 8 physical ports if available, standard integrated GPU, key Studio One/eMotion-like workflows, independent main/aux taps and real output mapping only when implemented. #15 not closed without physical evidence.

## 6. Delivery & communication format
After each meaningful milestone include: exact main SHA, merged PR, what newly works and what remains unsupported, screenshot on 1366/1440/1680, exact-head Chromium and Windows build/CTest result, native ZIP build provenance, 5-step hardware FAT and last known issue #15 status. Never say "DONE" if only planning, labels or CI passed. No rerunning ancient PR and no more R3 style-only releases.

**Immediate objective:** Make a user able to do on the ArSonMix MAIN window what the 9 screenshots demonstrate: choose real input/IO, retain the plugin insert view, toggle SENDS/ROUTE, route AUX return to MAIN, hear it on real hardware, and only afterward implement independent physical bus outputs.
