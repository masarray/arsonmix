# ENGINEERING HANDOFF v4 — ArSonMiX as realtime Studio One-class Console mixer
Date: 2026-10-31. **Execution plan, not a status claim.** Read in order:
1. [PRD v4](PRD_ARSONMIX_LIVE_STUDIO_CONSOLE_PARITY_V4_20261031.md)
2. [ARD v4](ARD_ARSONMIX_LIVE_STUDIO_CONSOLE_PARITY_V4_20261031.md)
3. [GUI v4](GUI_ARSONMIX_STUDIO_CONSOLE_V4_20261031.md)
4. [Feature & evidence matrix](STUDIO_CONSOLE_PARITY_MATRIX_V4_20261031.md)
5. Old [PRD v3](PRD_STUDIO_CONSOLE_DYNAMIC_LIVE_V3_20261031.md), [ARD v3](ARD_STUDIO_CONSOLE_DYNAMIC_LIVE_V3_20261031.md), [M3 handoff v3](HANDOFF_STUDIO_CONSOLE_M3_M5_20261031.md) only for bridge safety/evidence.
6. **Fresh** main SHA, changed PRs, CI and actual source before code. GitHub moves rapidly and old PR branches conflict.

## Explicit target and exclusions
Target EVERYTHING useful in Studio One's **Console and realtime mixer workflow**: hardware I/O, source→channel→insert/sends→group/FX→Main/Sub/Listen/Cue, master output, channel selection, copy/paste, mix routing/sidechain, control link/VCA, Scenes, VST3 preset/editor, GUI interaction, monitoring, performance and safety. Not DAW timeline/arranger/audio editor, track recorder/bounce, MIDI sequencer. No automatic OBS or virtual-cable dependency. Not a copy of proprietary GUI/music/plugin internals.

Official benchmark:
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Channel%20Overview%20and%20Editor.htm
- https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Scenes.htm
- https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Set_Up_Your_Audio_Device.htm

## Main snapshot during v4 authoring; ALWAYS refresh
At first read `6022a4fc0218decac694568d73c772a634eba37a`:
- #223/#231/#238 real native POST send + AUX gain, #240 DEVICE4 software source aliases, #241 raw capture while mute, #243 FAST1 explicit route ACK→hardware ARM all merged.
- #242 native control-side `createBusForSelected` with independent ChannelId + PCM; #244 M3B **merged** dynamic publisher with verified 16-lane offline native C++ tests; **live `LiveAudioEngine` still calls legacy `LiveDryRoutingLab::process`**. Don't confuse callback-ready with live switched-on.
- Current `NativeShowSchema` and browser arrays fix 10 CH. CH1 uses legacy vendor/Orbit VST ownership; native Master FX UI intentionally disabled, native PDC incomplete. M2 +BUS still borrows unused CH as interim.
- Exact main Windows and Chromium CI at this baseline are SUCCESS, physical FAT (issue #15, old 0FX crackle) still OPEN, no certified audible hardware/latency claim. Factory plugin lock `config/factoryfx-approved-releases.json` has empty plugins array. Do not bulk merge stale #225/#229 or previous callback branches.

## Anti-naive non-negotiable implementation rules
**One** master mix DSP graph per callback. Extend existing C++ source, do not rewrite from scratch or run old and new renders at the same time. GUI never owns PCM. One canonical native ChannelId roster/graph, plugin instance lifecycle, device epoch, request/revision ACK, and session store. GUI only projects accepted snapshot. RT callback: NO heap, mutex/blocking, disk, logging, WebView, enumerate, VST instantiation/prepare/delete, graph sort or unbounded spin. All new code must be bounded/snapshot-based, resource-budget admitted and small enough for low-end CPU/iGPU. Do not over-architect by introducing generic job schedulers or duplicate managers. Keep one old golden PCM test for each migrated subsystem. Always compare source HEAD and active PR owners.

## Milestone A — native production graph cutover (M3C; blocker for all dynamic GUI)
**Inputs:** `engine/core/DynamicChannelRoster.h`, `DynamicPreparedMixerBlock.h`, `DynamicNativeSessionPublisher.h`, `LegacyLiveGraphMigration.h`, `src/LiveAudioEngine.h`, `src/LiveDryRoutingLab.h`, `src/NativeShowSchema.h`, live FX lease modules.
**Work:**
1. Baseline existing real 10CH+4 AUX PRE/POST, CH1 legacy FX and remaining routed VST, fader/pan/mute/solo, input ordinal translation, native MAIN/MONITOR/secondary and Show. Build golden PCM with 0FX, CH1 VST bypass and stereo/mono, nonadjacent hardware L/R, send gains, solo and Master mono (AFTER plugin).
2. Build **one** ChannelId→FX lease adapter for all strips, migrate CH1 plugin ownership without closing vendor editor/Orbit or changing state chunk; prepare and tear down off RT. Preserve routing/Show identity and real physical outputs.
3. Adapt `DynamicNativeSessionPublisher` to active audio graph, preserve hard safety gates and source/secondary PCM taps. Switch existing callback to one prepared render producer under accepted epoch/revision; never sum two engines. Prepare retirement and "rollback to old accepted snapshot" before activation.
4. Ensure live native parameter updates cover fader, mute/solo, pan, POST send, AUX returns in new graph; sample dezipper deterministic, old UI ACK semantics preserved.
5. First 10CH+4AUX golden sample-by-sample PCM matched within appropriately justified FP tolerance; then activate 12 independent IDs + new Group + new FX Return with real actual VST3 processing and correct MAIN and MONITOR. Entire thing must be inside the **existing** callback (not C++ offline test only). Reject stale, malformed, cycles, duplicate routes, output-armed unsafe commit and resource budget.
6. Only after actual callback parity does new native session status advertise dynamic channel capability. Browser Add Channel stays disabled until that flag is true.
**Gates:** Windows MSVC build/CTest on exact head, native PCM A/B, instrumented zero callback allocation and bounded deadline, rollback/epoch regression, old Show snapshot preserve, 2 real physical mic→new Group/FX→MAIN/Monitor FAT when hardware is accessible. No breaking CH1/plugin identities.

## Milestone B — daily operator Studio Console (native ACKed, not placeholder)
**Ownership:** `web/app.js`, `web/bridge.js`, `web/mixer-io.js`, `web/monitor.js`, `web/mixer-layout.js`, `web/styles-src/`, generated standalone HTML, C++ native session commands.
**Work:**
1. Project native `AcceptedSessionSnapshot` keyed ChannelId; retire static `const channels=raw.map()` and fixed draft arrays as live truth without breaking demo-only static preview. Keep one routing controller in monitor.js. Keyed strip reconciliation; selected channel via stable ID not strip index, view order independent from source pin.
2. Implement Add Mono/Stereo Channel, Add Bus, Add Bus for Selected Channels, Add FX Channel, rename/reorder/duplicate/delete; all structural actions native transaction/ACK + guarded resource admission. Interop with existing M2 borrowed-lane sessions and old Show; do not silently rewrite accepted routes.
3. Implement Ctrl/Shift multi-select and one atomic `createBusForSelected`: selected inputs primary out exclusively to new Group, preserve parallel SEND, bus to MAIN; display actual fan-in and sender list, hide/show sources, selected highlight, OUT NONE/SEND ONLY.
4. Add complete per-strip `IN`, `OUT`, `SEND` selectors with actual verified device and signal path; drag native installed VST3 to SEND creates true FX Return with safe prepared lease, not fake JS catalog. FX insert move/copy/replace/sidechain via same native instance controller.
5. Consistent GUI role strips (Input/Group/FX/VCA/Main/Listen), pinned MASTER, compact/expand/narrow banks and channel search. GUI update events must not trigger full mixer DOM `innerHTML` during fader drags or meters; preserve layout and hover perf on ordinary Windows iGPU.
**Operator acceptance:** click Audio Setup choose MAONO/ASIO or standard WASAPI, START AUDIO→Mic1 accepted; select MIC1+MIC2→Add Bus, attach plugin to bus, create separate FX Return via SEND, hear accurate dry+wet PCM; no extra channel consumed and no fake status. Out-of-budget/cycle rejected with old audio intact.

## Milestone C — complete VST3 / PDC / Listen-Cue mixer
**Work:** unify native MASTER and sub-out FX lanes, render order and actual Master pan/mono, per-ChannelId vendor editor/Show leases, sidechain bus negotiation; externalized/plugin latency PDC (serial/parallel/nested sends) using off-RT delay-line preparation; monitoring performance and safe plugin > configured budget treatment. Build Listen Bus independent of MAIN with explicit Solo-in-place/PFL/AFL/Cue and dim; ensure independent physical output pins safe and no mixing feedback.
**Acceptance:** serial compressor + parallel reverb correctly aligned by impulse test; 2 mics/group/FX and MAIN native FX real (not disabled), PFL only headphones and never changes MAIN, plugin bypass/reorder clickless measured, zero false VST instance shares, sidechain layout rejects illegal cases. Compare true observed latency to driver buffers and plugin compensated latency.

## Milestone D — Show/Scenes, VCA/Groups, deeper Console workflows
**Work:** V5 dynamic Show keyed ChannelId with V2/V4 migration; per-scene scope/Recall Safe, atomic plugin chunk restore and rollback, device output mapping does not auto ARM; VCA (control-only gain offsets without summing PCM), link-group parameter memberships, copy/paste selected channel states, macro MIDI/OSC if safety tested; eventual FX routing Splitter with normal/channel/frequency splits and delay compensation. Physical hardware inserts only with measured loopback + user-approved safe output/input pair.
**Acceptance:** Create VCA Vocal to control 3 mics w/o new audio Bus; old Show loads into dynamic session preserving sends/vendor chunk; Scene recall excludes Master/physical outs when Recall Safe; one corrupt plugin chunk causes clean rejection with unchanged audible output. No timeline editor introduced.

## P0 hardware FAT (parallel gate, never postponed into fake release)
**Required physical scenario** (starting 0 FX) with ASIO and Windows Audio/MAONO/Realtek or available representative interfaces:
- Observe verified capture PCM under hardware MUTE and correct VB-CABLE direction where used. Request native patch and exact epoch/revision ACK, then explicit MAIN ARM and headphone-level audio. Verify left/right pins, phase, output mute and clipping latch.
- Check 44.1k/48k, reasonable physical buffer choices and WASAPI shared clock caveat, 0FX crackle issue #15, callback/xrun, capture discontinuities and output starvation; attach audio diagnostics and reproduce sound. Any audible crackle is a release FAIL independent of CI.
- Change routes/source formats, unplug/replug, plugin add/replace/bypass, save/recall, independent secondary destination, panic mute and reconnect. Verify deliberate rearm; record actual latency with loopback where possible, never claim zero milliseconds.
- On standard integrated GPU: pointer drag/hover, FX dialog, extended channel banks, 30Hz truth-based meter while audio armed; no mouse sluggishness, meter layout shift or notable callback spikes from UI.

## CI economy / Git safety
- One owner per shared file/PR. Read exact latest `main`, compare current PR `#244` lineage (if merged, never blindly remerge), prepare dedicated branch from main, preserve recent device/source/UI fixes. No stale full-file overwrite. Update AGENTS/read-first contract only when status actually changes; no duplicated competing roadmaps.
- Cheap local source/contracts, static JS syntax, golden native unit tests FIRST; then one meaningful PR with exact-head Chromium + Windows native/C++ CTest/ZIP. No workflow matrix fanout for documentation-only changes; don't waste CI for tiny cosmetic commits.
- For CSS/HTML: edit canonical modular `web/styles-src` owner; regenerate `web/styles.css`, `web/mixer-io.js` and matching standalone HTML script/style by approved deterministic build; parity regression required.
- CI is software proof, not physical evidence. PR body must state exact native audibility, mouse perf, plugin state and HW FAT separately. No unconditional "Studio One parity", "zero latency" or "production ready" badges until required acceptance is satisfied.

## Merge sequence
A native graph live cutover → B fully native operator workflow → C PDC/Listen/Master FX → D scenes, VCA and advanced routing, while P0 hardware reliability remains mandatory across all. Larger milestones > tiny UI churn; sequence can run non-conflicting ownership in parallel but NEVER merge stale engine/hardware branch without reconciliation.

## Final user FAT script (when executable is actually qualified)
Audio Setup choose actual ASIO interface, verified Mic1 and Mic2 mono inputs, MAIN physical stereo 1/2, click START AUDIO; verify Mic1/2 meter and audible clean MAIN at conservative gain, hardware mute safety. Select both mic channels→Add Bus for Selected→new Vocal Bus (not borrowed CH); insert real compressor on Bus; create FX Channel reverb via SEND while Mic direct OUT to Vocal Bus; select bus output to MAIN; use PFL/Listen on separate headphones and verify MAIN unchanged; save Show and two scoped Scenes; switch fader/POST-send live without crackle, test reconnect and panic mute; repeat WASAPI real output and optional secondary/virtual destination. Record exact build/artifact SHA + diagnostics, not synthetic screenshots.
