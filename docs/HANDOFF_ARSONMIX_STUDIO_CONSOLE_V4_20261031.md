# ENGINEERING HANDOFF v4.1 — ArSonMiX as realtime Studio One-class Console mixer
Updated: 2026-11-01. **Execution plan, not a status claim.** Read in order:
1. [PRD v4](PRD_ARSONMIX_LIVE_STUDIO_CONSOLE_PARITY_V4_20261031.md)
2. [ARD v4](ARD_ARSONMIX_LIVE_STUDIO_CONSOLE_PARITY_V4_20261031.md)
3. [GUI v4](GUI_ARSONMIX_STUDIO_CONSOLE_V4_20261031.md)
4. [Feature & evidence matrix](STUDIO_CONSOLE_PARITY_MATRIX_V4_20261031.md)
5. Old [PRD v3](PRD_STUDIO_CONSOLE_DYNAMIC_LIVE_V3_20261031.md), [ARD v3](ARD_STUDIO_CONSOLE_DYNAMIC_LIVE_V3_20261031.md), [M3 handoff v3](HANDOFF_STUDIO_CONSOLE_M3_M5_20261031.md) only for bridge safety/evidence.
6. **Fresh** main SHA, changed PRs, CI and actual source before code. GitHub moves rapidly and old PR branches conflict.


## v4.1 EXECUTION LOCK — 2026-11-01 (authoritative; old A/B/C/D tasks below are background)

**Starting GitHub fact:** \`main@9f66d085\` (FAST2 #259 / FAST3 one-APPLY, #258 stereo peaks, #260 atomic PAN, #261 common native FX draft merged; Windows combined CI pending when audited, Chromium passed). Today's live callback still executes 10 fixed input + 4 AUX with occupied CH1 vendor FX legacy path. Dynamic roster/publisher PCM is off-live; #263 DRAFT accepts only one dry GROUP if no FX/AUX/sends/MONITOR/Show. **Do not merge #263 as full live dynamic mixer.** #251 default-OFF beta de-click is not a proven #15 cure. This section supersedes 2026-10-31 commit/PR/START-AUDIO instructions lower in the file.

### Stop the progress loop — two large integration deliverables, physical P0 in parallel

**GATE 0: evidence + rollback baseline (part of M-U1, not standalone PR).** Freeze exact \`main\` after each merge; enumerate existing CTest and UI contracts, record current 10CH+4AUX signal order and legacy/CH1 VST ownership. Add golden PCM and vendor-chunk preservation tests *before* removing fixed route or CH1 fallback. Include accepted Source/Output routing, sparse pin masks, PRE/POST send/aux, Master mono, MONITOR/secondary and old Show. Commit tests in SAME milestone as engine changes to avoid another paperwork-only "progress".

**M-U1 — Universal live DSP cutover preserving working audio.** In the existing JUCE callback, use ONE accepted dynamic ChannelId schedule for all existing 10 channels, 4 AUX returns, MAIN/MONITOR and optional secondary PCM tap; migrate CH1 legacy VST3 vendor/Orbit/editor state and CH2..10 native plugin instances to shared \`(ChannelId,InsertId)\` ownership; same per-ChannelId fader/mute/solo/PAN and send params. Restore legacy Show through versioned adapter. All current native working features must remain functional **simultaneously** in a normal session. Do not replace 10CH graph with a dry-only GROUP fallback; do not instantiate or destroy VST inside callback. Prepare/commit/retire transaction under epoch/revision and audio safety gates. **Exit:** golden PCM A/B parity with legacy at 0FX + representative real VST, negative ACK/rollback/hotplug, old Show preservation, exact-head Windows/Chromium PASS and FAT-ready artifact. M-U1 is not DONE if only C++ offline tests pass or any existing AUX/Monitor/FX must be deleted.

**M-U2 — Universal operator console, usable source-to-speaker journey.** \`+ CHANNEL\` MONO/STEREO on an actual ChannelId at CH11 or later; GROUP for 2 selected active FX-bearing channels; separate native FX RETURN from parallel POST send; choose legal primary destination and verified MAIN/MONITOR hardware outputs; save/restore portable Show. Native accepted snapshot drives narrow keyed GUI strips, native FX Rack and per-lane meter. Fixed 10-array JS can remain for *preview-only* migration but must never authorize accepted routing. **Exit:** two live mics + stereo playback + independent new GROUP compressor + FX RETURN reverb and existing AUX sends produce audio through fader/MUTE and MASTER, with no double-dry, no borrowed CH, no plugin/editor/state loss, safe DEVICE/APPLY rejections, no iGPU GUI stutter, exact-head native/UI CI and hardware FAT candidate. No FX-bypass / channel-specific workflow distinction between CH1 and CH11.

**P0 physical track — runs alongside both milestones, independently blocks public stable release.** Issue #15: prove audible dry source actually passes through ArSonMix (channel fader/MUTE/MASTER all control sound), distinguish MAONO/direct monitoring bypass from true MAIN; A/B same-clock wired ASIO vs WASAPI/virtual capture/other endpoint, test zero FX before VST; capture actual endpoint identity, 44.1/48 kHz buffers, 30-min clean 0FX, capture discontinuities versus render xrun separately; no false DSP overload attribution. If crackle persists with strong DSP headroom, fix driver/clock/reservoir mechanisms with evidence instead of raising arbitrary buffers or merging #251 beta by default. Hardware test needs user running exact-sha Windows artifact; CI doesn't do acoustic listening.

**M-U3 (after M-U2):** native MASTER VST FX and pan, plugin bus layout + sidechain, bounded latency compensation for parallel buses/returns, independent Listen/PFL/AFL/Cue, dynamic V5 Show/Scenes/Recall Safe and control-only VCA. Splitter/advanced tools only after basic mixer and hardware correctness. No DAW arranger, timeline or MIDI editor.

### Integration ticket and PR/merge rules
- One meaningful integration branch from exact \`main\` per M-U1 or M-U2 (not 10 overlapping CH-by-CH PRs); one owner per overlapping native callback, VST registry, Show schema, JS route controller. Reuse existing tests, avoid duplicate fake graph API layers or a second native callback.
- Every PR must state: exact base SHA; production callback path; what remains audible; saved vendor VST state; actual mono/stereo/FX/sends/output behavior; conflict inventory (#263/#251); negatives/rollback; deterministic test results; Windows/Chromium exact-head links; hardware qualification status. "CI PASS" alone means candidate, not stable.
- Gate changes with cheap native unit/JS syntax/contract tests locally, then ONE Windows MSVC/CTest/ZIP + Chromium check pair for meaningful code. Documentation-only revisions do not need to trigger heavy compilation. Reject non-evidence micro-PRs and stale overlapping PR merges.
- Do not disrupt the approved L2 console baseline (aligned compact PNG faders, pinned MASTER, BTN-180, clear fixed-zone meters, no hover blur). Use on-demand keyed strip rendering and bounded telemetry updates on modest Windows iGPU.
- Keep **FAST3 single AUDIO I/O APPLY** and emergency MUTE. One APPLY may internally sequence safe device MUTE→route ACK→guarded ARM, but stale epoch/device changes must fail-silent. Everyday fader and qualifying send changes require neither a global MUTE nor another APPLY.
- Freeze "new feature marketing": mark production code LIVE, restricted pilot EXPERIMENT, tested prepared kernel OFFLINE; don't claim physical zero latency or native mixer sound until physical sound follows actual fader and hardware output.
- Benchmark actual public operator workflows: [Studio Pro Console](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm), [FX/send bus workflow](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm), [Adobe Audition bus/send/mix](https://helpx.adobe.com/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html) and [eMotion LV1 patch matrix](https://www.waves.com/emotion-lv1-tutorial-2-2-using-the-patch-window). Do not infer undocumented closed-source engine internals.

### Acceptance test operator sequence (when M-U2 native build exists)
1. In **AUDIO I/O** choose available wired ASIO or Windows Audio device, real stereo output and 2 independent mic pins; press **APPLY once** (no START AUDIO) and confirm accepted driver/epoch/output and distinct RAW/POST/MAIN PCM.
2. Play known signal into CH1 mono, CH2 mono and CH11 stereo; set gain low, prove channel fader, channel MUTE and MASTER mute control *audible* output. If audible while MASTER MUTE, isolate external/direct monitoring; stop test.
3. Put a real VST3 on CH1 and CH11 and verify actual plugin editor/parameters, persistence, bypass and channel stereo L/R truth. Select CH1+CH2→Add Bus for Selected while both retain FX/AUX/monitor, check independent Group receives both exactly once. Group −6 dB must affect MAIN without moving channel faders.
4. Create independent FX RETURN via POST SEND, insert real VST3 reverb, preserve group-to-MAIN dry path, vary send/return gains without crackle or driver reopen. Deliberately test cycle, stale ACK, unsupported plugin bus format and out-of-budget addition; old audio remains accepted/unchanged.
5. Save/recall dynamic Show with new ChannelIds, user-selected output device/profile, Group and FX Return plugin chunks. Intentionally remove/reconnect device, verify silent safe failure and deliberate re-ARM from one APPLY. Attach exact-sha artifact provenance, relevant CPU/capture/render telemetry, and hardware A/B 0FX soak logs.

---

## Explicit target and exclusions
Target EVERYTHING useful in Studio One's **Console and realtime mixer workflow**: hardware I/O, source→channel→insert/sends→group/FX→Main/Sub/Listen/Cue, master output, channel selection, copy/paste, mix routing/sidechain, control link/VCA, Scenes, VST3 preset/editor, GUI interaction, monitoring, performance and safety. Not DAW timeline/arranger/audio editor, track recorder/bounce, MIDI sequencer. No automatic OBS or virtual-cable dependency. Not a copy of proprietary GUI/music/plugin internals.

Official benchmark:
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Channel%20Overview%20and%20Editor.htm
- https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Scenes.htm
- https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Set_Up_Your_Audio_Device.htm

## Historical main snapshot during v4 authoring; superseded by v4.1 above
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
**Operator acceptance (updated FAST3):** click AUDIO I/O choose verified MAONO/ASIO or standard WASAPI, one APPLY→accepted guarded source/output; select MIC1+MIC2→Add Bus, attach plugin to bus, create separate FX Return via SEND, hear accurate dry+wet PCM; no extra channel consumed and no fake status. Out-of-budget/cycle rejected with old audio intact.

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


## Historical merge sequence (superseded by v4.1 two-deliverable execution lock)
A native graph live cutover → B fully native operator workflow → C PDC/Listen/Master FX → D scenes, VCA and advanced routing, while P0 hardware reliability remains mandatory across all. Larger milestones > tiny UI churn; sequence can run non-conflicting ownership in parallel but NEVER merge stale engine/hardware branch without reconciliation.

## Final user FAT script (when executable is actually qualified)
AUDIO I/O choose actual ASIO interface, verified Mic1 and Mic2 mono inputs, MAIN physical stereo 1/2, click APPLY once and confirm native ACK/guarded output; verify Mic1/2 meter and audible clean MAIN at conservative gain, hardware mute safety. Select both mic channels→Add Bus for Selected→new Vocal Bus (not borrowed CH); insert real compressor on Bus; create FX Channel reverb via SEND while Mic direct OUT to Vocal Bus; select bus output to MAIN; use PFL/Listen on separate headphones and verify MAIN unchanged; save Show and two scoped Scenes; switch fader/POST-send live without crackle, test reconnect and panic mute; repeat WASAPI real output and optional secondary/virtual destination. Record exact build/artifact SHA + diagnostics, not synthetic screenshots.
