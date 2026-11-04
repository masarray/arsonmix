> **SEQUENCING SUPERSEDED:** [PRD v5.1](PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md), [ARD v5.1](ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md), and [Puzzle Contract v8](PUZZLE_INTEGRATION_CONTRACT_V8_20261101.md) now govern parallel work and final acceptance. Keep this v7 document as release-train history; do not follow v7 dependency waterfall over v8's producer→consumer socket.

# ArSonMix — ONE PRODUCT RELEASE TRAIN v7 (2026-11-01)

**Binding delivery orchestration, not a shipped-feature claim.** PRD v5 + ARD v5 define functionality and native architecture. Existing execution lock v6 retains safety rules. This v7 document supersedes earlier workstream sequencing and partial user-testing invitations.

## Product commitment — ONE COMPLETE WINDOWS LIVE DIGITAL MIXER BEFORE USER UAT
The owner should not debug another half-completed EXE. Internal developers must first build and integrate a coherent operator-facing realtime mixer with the Console functions of Studio One, Adobe Audition Mixer and Waves eMotion LV1: visible IN / processing M-ST / ordered genuine VST3 FX / PRE-POST SEND / independent OUT / PAN / MUTE / SOLO / accurate native meters and faders, dynamic channels, Group, parallel wet FX Return, independent Monitor, Show and lightweight adaptive GUI. This is functional Console parity, NOT timeline/audio editing, MIDI, recorder or copyrighted visual cloning. No claim of 0ms physical latency.

No requests for customer First Sound, hardware debugging or half-feature beta testing. Internal Windows CTest and UI simulation first; an independently equipped engineer/QA rig must complete physical qualification when available. If the rig is unavailable, explicitly mark hardware FAT PENDING, not passed; do not shift early QA onto the customer. User UAT is invited once after G3 integrated feature readiness and truthful reporting.

## CURRENT GitHub collision ledger (check live HEAD before any change)
- main e5a3d076: native #269 offline accepted ChannelId PCM golden tests merged; production still legacy fixed M4 10CH+4AUX.
- ENGINE #266 is the single native implementation. GUI #271 / work/operator-console-v6 is the single primary Console UI implementation. FAT #270 owns evidence and workflow YAML. #192 is the cross-owner schema and merge control; #273 first-sound defects and #15 physical crackle remain blockers.
- Native integration #272 is a reusable codec/test patch requiring ENGINE adoption into its actual JUCE WebView dispatcher; a green header CTest does not create the dispatcher.
- #274 is a generated HTML parity patch stacked on GUI branch. #275 is an overlapping M4 First Sound UI improvement: GUI owner must selectively reconcile its useful work into #271, never promote an independent second GUI.
- #276 packages a partial First Sound Windows candidate: an INTERNAL staging experiment, not a universal-console finished product or customer UAT. FAT owner must not declare it ready for the user ahead of ENGINE full-fidelity callback migration.
- Separate green tests of #266/#271/#270 never equal one integrated successful SHA.

## THREE OWNERS ONLY
ENGINE thread: PR #266, owns engine/core/**, src/**, C++ DSP/device/bridge tests, native one-callback VST ownership/telemetry/Show. Never modify web/**.
GUI thread: PR #271, owns web/**, UI tests, generated standalone HTML using existing CSS/HTML generator. Never modify src/** or engine/core/**; no separate JS PCM/router.
INTEGRATION/RELEASE thread: PR #270, #192 and controlled promotion branch, owns cross-layer schema tests, CI workflows, Windows packaging/provenance and internal physical FAT; never forks GUI/engine production code. #272 reviewed by ENGINE, #274/#275 absorbed by GUI, #276 staging only.

## FROZEN DATA CONTRACT — one actual native authority
The ENGINE control thread implements arMixConsoleSubscribe -> full accepted ConsoleSnapshot, arMixConsoleIntent -> native control request and arMixConsoleAck -> explicit ACCEPTED/REJECTED/REQUIRES_DEVICE_CHANGE. The browser is only a projection.
Intent example: {requestId:"unique-session:1",deviceEpoch:"69",baseGraphRevision:"3",baseRosterRevision:"4",operation:"SET_SOURCE",targetChannelId:"7",payload:{...}}.
Every ChannelId, device epoch and graph/roster revision is a CANONICAL DECIMAL STRING (native uint64; browser BigInt comparison), NOT a JavaScript Number or strip ordinal. RequestId is a unique-per-editor-connection opaque printable string (not reset-only "1"), echoed byte-for-byte in ACK. No duplicate/replay/stale-epoch/stale-graph accepted.
Snapshot: {schemaVersion:6,productionCallbackLive,device:{epoch,backend, captureDevice,playbackDevice, enabledInputs, enabledOutputs, inputEndpoints,outputEndpoints,sampleRate,bufferFrames,outputArmed},graph:{revision,rosterRevision,channels,destinations,capabilities:{supportedOps,dynamicChannels,groups,fxReturns,monitor}},telemetry:{rawCapture,channelPCM,mainPCM,monitorPCM}}. Field names, allowed operations and native fail-closed behavior must be version-tested against the ACTUAL ENGINE producer; mock fixtures must be generated from this contract, not a competing schema. ACK alone is never physical PCM/LIVE; wait for same-epoch fresh accepted snapshot.

SOURCE (physical MONO/STEREO pins) and PROCESSING M/ST (channel width) are separate: a mono mic can feed stereo processing but cannot pretend physical stereo IN1/IN1. Verified input/output pins are obtained from active backend mask and mapped to callback ordinals, not invented IN2 or adjacent L/R. A physical route is separate from serial primary Group OUT and parallel wet FX Send. Monitor outputs are separately selectable legal device pins. All RT callbacks remain allocation/lock/scan/GUI free, structural graph/VST edits prepared off callback with rollback; fader/mute/solo/pan/send continuous controls stay RT-bounded.

## INTEGRATED GATES — all three threads work in parallel WITHIN each gate
### G0 — source/branch reconciliation and wire freeze
ENGINE: incorporate validated #272 schema into sole native bridge design, identify exact production cutover/CH1/Show blockers, publish actual ACK and supportedOps inventory.
GUI: reconcile #274 generated parity and #275 direct First Sound improvements into #271, remove duplicate wizard paths, ensure unique request IDs and accepted-snapshot-only LIVE.
FAT: owns single combined Windows and Chromium workflow YAML, JSON contract fixture from native writer; reconcile #276 as staging only. Verify no cross-owner file collision.
EXIT: one versioned schema, no competing UI/audio renderers, all three owners understand native ops and feature gating.

### G1 — ONE real production callback + FIRST SOUND, NOT user testing
ENGINE: full M4-fidelity migration of occupied CH01 vendor VST3/Orbit/editor, CH02-10 independent FX, four AUX, sends, MAIN/MONITOR/secondary, Show into one ChannelId JUCE callback. Real active device capture/output pins, raw/channel/main PCM telemetry and Intent/ACK.
GUI: one compact Console home with direct per-strip IN/OUT, single DEVICE drawer; advanced crosspoint/Software Input alias hidden and optional. One verified pin -> IN1 MONO only and clear NO RAW if unfed; no stereo IN1/IN1; no multi-APPLY workflow for normal routing.
FAT: internal native actual-callback PCM test (CH07 IN1, CH02 IN2 when verified), single-pin/NO PCM, native-source rejection/rollback, MAIN mute, hotplug, explicit VB-Cable input feeder diagnosis, 0FX/2FX parity.
EXIT: genuine independent native audio callback and operator-first GUI agree on the same accepted data; no customer test invitation.

### G2 — full core professional Console functionality
ENGINE: real dynamic CH11+, Group Selected serial PCM with exclusive primary OUT, parallel wet FX Return PRE/POST sends, independent MONITOR to legal physical/virtual output, native per-role VST3 rack load/reorder/bypass/editor, Show with vendor chunks/IDs.
GUI: every native role gets working IN (where legal), M/ST, inserts, sends, OUT, fader/meter, mute/solo/pan, dynamic banks, +CHANNEL, group and FX Return creation and pinned MAIN; all operations display accepted ACK and errors correctly, no dead or decorative controls.
FAT: golden PCM for real inserts/Group/wet Return/monitor, no double-dry, channel/lease stability, Show and failure rollback. Performance and VST failure scenarios covered internally.
EXIT: full user-requested LIVE CONSOLE core feature matrix operational end-to-end in one integration tree.

### G3 — performance, full integration and release qualification
ENGINE: enforce real-time heap/lock rules, no click on edit/drop/bypass, device and clock fail-safe, deterministic tests with real/native callback.
GUI: responsive GPU-light 1024x600 / 1366x768 / 1680x900, no blur-heavy/hover lag, no relayout on drag; correct mono and independent L/R LED meters and compact consistent faders.
FAT: resolve combined SHA conflicts, CSS/HTML generator parity, Chromium/Playwright+Windows MSVC/CTest and package on ONE exact SHA; Windows EXE checksum, source provenance. Internal real hardware 0FX & full FX QA and 30min crackle #15 as available, disclose unverified hardware honestly.
EXIT: integrated, feature-complete, internally qualified EXE with factual remaining limitations; no separate-branch PASS accepted.

### G4 — ONE UAT/release candidate
Only now ask the customer to test the finished mixer once, with one EXE and clear device setup; never send G1/G2 partial artifacts as product. Hardware FAT remains pending if no independent rig exists. Do not claim clean sound or parity where evidence is absent.

## PER-OWNER REPORT AND CI RULES
Each milestone report to #192 MUST include exact SHA and changed files, currently REAL capability vs stub/demo, native PCM/GUI evidence, remaining inter-thread dependency, and integration action. No cosmetic "milestone completed" claim. Targeted tests locally/cheap first, consolidated Windows+Chromium gate only at meaningful slice, read exact logs and fix root cause rather than blindly rerun CI. FAT retains ownership of .github/workflows/*.yml; GUI retains source/generator; ENGINE retains callback. Old PRs are reconciled into these owners, not multiplied.

## ACCEPTANCE MATRIX — no owner FAT request until all core stages complete
1. Real interface (ASIO/WASAPI) chosen once, 1 pin correctly exposed, silent input correctly diagnosed, no invalid stereo, direct CH IN and verified physical OUT.
2. IN1 -> arbitrary CH07 and IN2 -> arbitrary CH02 (on two-port rig); two actual independent VST3, bypass/reorder/editor, accurate audio and channel meters.
3. Add live CH11, group selected channels to GROUP->MAIN without double-dry, parallel FX Return wet SEND PRE/POST, Monitor to separate compatible physical output.
4. Real mute/solo/fader/pan, Main/Cue controls and one safe device reconfiguration; no callback heap/crackle/unsafe auto-arm.
5. Show save/recall with stable ChannelId, VST3 vendor chunks, pin mapping remap prompt/rollback.
6. One coherent fast UI (not 3-tab wizard), bundled native WebView HTML parity, one exact SHA ZIP/EXE and internal qualification evidence; user UAT invited only at G4.
