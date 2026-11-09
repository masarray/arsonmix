# Agent Workpack — ArSon Player Native Music / Setlist
**Date:** 2026-11-09 · **Scope:** executable vertical implementation plan from [PRD](../PRD_ARSON_PLAYER_LIVE_SETLIST_UX_20261109.md) and [ARD](../ARD_ARSON_PLAYER_NATIVE_AUDIO_ARCHITECTURE_20261109.md) · **Status:** PLANNED, NOT IMPLEMENTED. **Delivery policy:** [Autonomous Fast Delivery Contract](../AUTONOMOUS_FAST_DELIVERY_CONTRACT_20261109.md) is binding; no standalone M0 PR or approval. **M0 and M1 form one continuous native + GUI integration effort with an early testable Windows FAT artifact.**

> **Read [AGENTS.md](../../AGENTS.md), [Smart CI](../SMART_CI_RUNNER_WORKFLOW.md), and newer [Zero-Friction Smart Workflow ADR](../ADR_ARSON_PLAYER_ZERO_FRICTION_SMART_WORKFLOW_20261109.md).** Binding owner requirement: import → Play → MUSIC PCM to verified MAIN automatically, no Lab, ARM, MONITOR, Apply, checkbox, routing approval or second Start. Preserve user-custom SOURCE ONLY/BUS and unknown physical-output safety. Design only; re-fetch current main/active PRs.

## 0. One-sentence delivery contract

**Operator-first contract:** Drop music, click Play once, hear audio on same verified MAIN. Control-plane silently creates/reuses owned MUSIC ChannelId, binds PLAYER A and one factory MAIN WIRE, with real native ACK and one JUCE callback. No extra engine, manual patch, Lab, ARM/MONITOR or approval dialog.

## 1. Dependencies and commit strategy

- **Foundation:** engine source binding must distinguish `physical` from `playerDeck`; compiler/graph migration must preserve P0 hardware input/WIRE/FX/MAIN behavior and sample-accurate source fan-out.
- **UI authority:** native compiled asset is derived from approved `/preview/` assembler; do not work in old `web/index.html`, ship a second player website, or overwrite user-approved mixer artwork.
- **Concurrency:** PR #468 release alpha.6, #467 Harmony, and #469/#474/#476/#479 Vocal Throw are out-of-scope. Audit current status again; no cherry-pick or speculative conflict resolution. Preserve GUI/native product only after on-main reconciliation.
- **Suggested source branch:** `feature/arson-player-native-source-m1-YYYYMMDD` from fresh main; integrate one meaningful vertical milestone and tests per PR. Next milestones can follow cleanly without fragmenting dozens of parallel PRs. Do not create a duplicate active integration branch if player implementation work already exists.
- **No backend bypass, but smart defaults:** Auto-provision logical MUSIC→MAIN only on newly owned factory route and already verified output. Never switch to unknown physical hardware or override intentionally edited BUS/SOURCE ONLY, MAIN mute, or operator FX. Fresh Play on same verified output is sufficient internal authorization.

## 2. Milestone / independently testable vertical slices

### M0 — Embedded engineering groundwork for M1 (same product PR, no intermediate approval)
**Goal:** capture actual current render path and make player integration design non-speculative.

Tasks:
1. Re-fetch `main`, README, AGENTS, `src/Main.cpp` (JUCE callback & mixer command), `src/LiveAudioEngine.h`, `engine/core/DynamicPreparedMixerBlock.h`, `engine/core/DynamicNativeSessionPublisher.h`, `engine/core/DynamicChannelRoster.h`, `src/NativeMixerSessionDocument.h`, `integration/ApprovedNativeControlIntent.h`, `web/approved-native-bridge.js`, GUI builder scripts and relevant tests.
2. Trace and document observed `PCM producer → graph compiler → render publisher → callback → device` path and any unused staging code; test baseline Hardware IN → CH → FX → WIRE → MAIN, SOURCE ONLY, independent secondary output, device epoch restart.
3. Build a minimal deterministic offline virtual-source fixture to prove accepted graph can process a synthetic stereo stream on a ChannelId **without** fake physical pin indices. Verify legacy physical inputs bitwise unaffected.
4. Before UI, build deterministic pure `ensurePlayerFirstSound` planner and fixtures: new/existing MUSIC ownership; FACTORY vs user BUS/SOURCE ONLY; MAIN mute; verified vs unknown device; device epoch; repeated Play x100 creates no duplicate channel/edge. Then choose typed `SourceBinding`, prove one FIFO advance for multi-consumer and unchanged hardware PCM golden baseline.

**Exit gate:** legacy input PCM unaffected, native virtual source works, planner auto-provisions exactly one MUSIC→verified MAIN route; user custom routes remain; unknown device cannot be chosen silently. No extra callback or Lab, ARM, MONITOR, approval checkbox in intended operator journey.

### M1 — First playable deliverable in the SAME product integration PR as M0
**Goal:** usable integrated player with WAV/MP3/FLAC verified formats and manual track transition, **no pitch GUI promise before DSP**.

Native:
1. Introduce `engine/player/PlayerContracts.h`, `PlayerTransportState.h`, `PreparedPlayerSource.h`, `SourceBindingPlan.h` (names may change if a better existing owner exists).
2. Implement native controller, decode worker and **PlayerFirstSoundCoordinator**: single Play prebuffers, creates/reuses MUSIC ChannelId, attaches PLAYER A, commits owned factory WIRE→already verified MAIN when not operator edited, then sample-boundary transport and fade; one existing callback, never another device manager.
3. Establish thread and fault tests including decoder stall, rapid STOP/LOAD and rejected source selection; ensure no heap, disk, locks, waits, window, logs, plugin operation in callback.
4. Add `PlayerMediaStore` versioned portable Library/Setlist storage and relink safety; default STOP_AFTER_SONG, separate next selection, never delete a source file.
5. Extend native player intents/readback with graph/device/deck generation, `routeOrigin`, `routeAudible`, `physicalOutputReady` and one actionable inline fault. No fake Playing or admission/PATCH/ARM setup.

UI:
6. Extend the **approved Pages source and assembler** with toolbar `Player`, mini transport + compact non-modal left drawer, tabs SETLIST/LIBRARY/QUEUE, dense rows & +Music; simple transport current/next/ACK and intuitive error labels. Keep MAIN visible and one renderer.
7. Keep approved premium CSS: one Add Music/drop empty state, auto-generated setlist and Play controls; Library/Queue contextual, no huge cards, Lab mode, native context menu regression or checkbox wizard.

**Exit gate:** novice B01–B13: drop one song → one Play → native PCM via MUSIC→verified MAIN without PATCH, ARM, MONITOR, confirmation or second Start. 20-file auto-setlist; repeated Play/Stop x100 no duplicate route, SOURCE ONLY and BUS preserved, missing output one contextual selection; 44.1/48/96k, mic ASIO/decoder starvation tested. **At the first working M1 vertical milestone**, run fast UI/native then exact-SHA **ui + fat** once to produce a verified UNQUALIFIED user-test Windows ZIP/installer; **never** add duplicate `native` to FAT. Physical listening remains PENDING until witnessed. Omit pitch controls until DSP actually works.

### M2 — Pitch + speed with quality evidence
**Goal:** native continuous -6…+6 semitone and 80…120% speed, independent of mic sample clock and with click-free change.

1. Benchmark Signalsmith Stretch pinned version against another candidate on actual instrument, percussion and dense stereo tracks; compare DSP CPU, artifacts, stereo correlation, startup, seek latency and worker headroom. Verify MIT notice, repo AGPL compatibility, all three OS toolchains, optimized build.
2. Implement worker-side DSP adapter; generation-tagged pre-render ring, buffered output, resampling, de-click on changes and worker latency measurement. Avoid `AudioTransportSource` file calls from ASIO callback.
3. Add independent pitch and speed sliders/stepper with compact same-style controls; `KEY -2 st`, `TEMPO 100%`, `RESET`; persist per song and per setlist entry override; show actual applied ACK and pending state when buffering.
4. Validate tone sine fundamental ratio, duration ratio, transients, no abrupt pitch drops/clicks, repeated edits, stereo, EOF/stretch drain, 64/128/256 buffer; 30min mixed singer recording at physical ASIO when possible.

**Exit gate:** audible and fixture-confirmed pitch/tempo independent, zero mic speed/pitch effect, bounded callback. If CPU/quality insufficient, preserve M1 original-speed player and keep pitch feature gated.

### M3 — Live safety and advanced setlists
**Goal:** singer show workflow: history, cue, markers, queue and controlled autoplay.

1. Add advanced marker/trim loop contract and Live Seek Lock, without audio-thread disk I/O.
2. Add named/section setlists, optional STOP ON BREAK, per-entry end behavior, history and Play Next.
3. Preload NEXT generation on worker, add explicit Auto Next, optional per-song fade/gap without artificial silence for manual mode. Music sources remain independently routable through mixer; no hidden PFL leak.
4. Improve missing file Relink, portable export/collect media with explicit user authorization; performance-safe notification not popups mid-song.
5. Run loss-of-device and corrupt-file fault injection; native output never falsely auto-rearms.

**Exit gate:** performer can safely click random songs/queue during playback; active song unaffected; automatic transition only when on, respects break; status and IDs accurate.

### M4 — Dual deck/true crossfade / optional professional extras
**Goal:** optional seamless music-bed; do not burden singer default panel.

1. Add a separately prepared Deck B (same typed source family, isolated ring/transport/generation).
2. Route A/B to separate ChannelIds or a selected common music channel using declared policy; implement true overlapping envelopes in sole native render.
3. Add compact Advanced two-deck transport/crossfade with native PFL isolation (if supported), no second output authority.
4. Optional BPM/key analyzer with confidence provenance, MIDI/foot control, and stems later; no automatic sync to Vocal Throw unless validated and enabled.

**Exit gate:** A/B overlap, stable mic latency, no file swap in callback, no unexpected OBS/PFL leak, CPU/ASIO soak. This is deliberately post-M1/M2.

## 3. Suggested source touchpoints and tests

| Area | Inspect/edit | Minimum test artifact |
|---|---|---|
| Source graph | `engine/core/DynamicPreparedMixerBlock.h`, `DynamicChannelRoster.h`, `DynamicNativeSessionPublisher.h`, `src/LiveAudioEngine.h` | `tests/player_virtual_source_graph_test.cpp`: legacy physical parity, source-only, double fan-out, invalid route |
| Prepared PCM ring | `engine/player/PreparedPlayerSource.h` new, `src/PlayerDecodeWorker.h` new | `tests/player_pcm_ring_test.cpp`: starvation, stop/seek generations, no callback alloc or lock |
| Transport | `engine/player/PlayerTransportState.h` new | `tests/player_transport_state_test.cpp`: all transitions, stale intents, EOF/auto/break |
| Persistence | `src/PlayerMediaStore.h`, `src/NativeMixerSessionDocument.h`, `src/PortableWorkspace.h` | `tests/player_store_roundtrip_test.cpp`: 2 setlists same song, relink, migration, interrupted write |
| Pitch/time | `src/PlayerPitchTempoProcessor.h` new | `tests/player_pitch_tempo_pcm_test.cpp`: st ratio, frame duration, multirate stereo/clicks, CPU budget |
| Native GUI bridge | `integration/ApprovedNativeControlIntent.h`, `src/Main.cpp`, `web/approved-native-bridge.js` | `tests/player_native_bridge_contract_test.py`: ACK vs native truth, stale generation, no WebAudio |
| Approved GUI | `site/preview-v2/index.html`, matching `assets/`, `scripts/build_pages.py`, `scripts/build_approved_native_gui.py` | Chromium screenshot/layout + user gestures: 1366/1440/1680/1920 and native demo separation |
| Hardware/secondary | `src/LiveAudioEngine.h`, `engine/device/LiveSecondaryOutputDevice.h` | FAT checklist: ASIO first sound, mic+music mixing, VB Cable/OBS, disconnect & reconnect |

**Keep real files/paths matching actual repo structure:** use table as agent routing hints; if one test fits existing CTest target better than a new binary, extend it. Do not add duplicate native backend or hidden multi-agent ownership.

## 4. Baseline and test profiles

```text
Synthetic audio:
  440 Hz sine mono/stereo + -2st/+3st + 90/110% speed
  stereo drum transients + quiet acoustic guitar + dense vocal mix
  44.1k/48k/96k input and device rates; 64/128/256/512 frame callback
  2x ChannelId same deck, 1x hardware mic, BUS A → MAIN, source-only graph

Real performance:
  Play backing song, mic monitoring and live EQ/Comp/VST chain
  Change setlist selection and queue during current song; no current skip
  WIRE disconnect & reconnect, SOURCE ONLY retains source VU, MAIN silent
  Stop/Play/Seek/Load 100x, pitch/key changed repeatedly
  ASIO primary, secondary virtual output/OBS, delay/FX rack on vocal
  Device stop/restart with saved session; portable restore & missing media
  30min soak, CPU and buffer metrics + independent music/vocal listening
```

Measure: click samples (>chosen test threshold, disclose), pitch ratio in cents tolerance, output-rate count, generation drop counts, PCM meters and callback CPU vs baseline, memory bound, underruns, UI latency and source route truth. **Never claim audio listening from simulation alone**.

## 5. Agent “definition of done” checklist

- [ ] Actual fresh main, open PRs, approved GUI, accepted PCM authority and baseline read before edits.
- [ ] Each milestone has one vertical test fixture *and* product-visible behavior, not just skeleton files and CSS.
- [ ] No extra callback, WebAudio, browser audio, duplicate source graph, fake input pin or music loopback.
- [ ] Play finishes owned MUSIC source/channel/default trusted MAIN routing and actual native transport ACK in one gesture, or provides one contextual actionable failure (not another ARM/Apply).
- [ ] SOURCE ONLY/custom BUS/WIRE/VST3 and mute/solo remain authoritative; auto route only on factory-owned MUSIC ChannelId and verified MAIN; never select unknown hardware.
- [ ] Dead or missing media does not block hardware and does not delete playlist entries.
- [ ] Existing main mixer available and correct when Player pane is hidden.
- [ ] Library/Setlist/Queue persist, launch never auto-PLAY; fresh Play on exactly matching known output prepares internally without secondary ARM/START; new endpoint requires one contextual Choose output action.
- [ ] Relative paths and exact source identity verified; no playback of an unrelated relinked file.
- [ ] Audio block is allocation-free, no locks/disk/FFT/decoder/JS and cannot let worker starvation stall ASIO.
- [ ] Visually fits user-approved `/preview/`, no huge cards, giant font, crowding, or displaced MAIN fader.
- [ ] Relevant Linux/Windows/macOS, browser and real hardware tests logged against **exact commit SHA**.
- [ ] Build package has exact source provenance and clean install; actual physical FAT separately marked PASS/PENDING.
- [ ] PR states changed files, test evidence, constraints/limitations, branch, HEAD, main SHA, next milestone; no fake shipped player claims.

## 6. Workflow/CI cost policy

M0 in SAME M1 PR: add native source fixture and first-sound planner with fast checks; continue independent M1 engine/bridge/GUI development while fast CI runs, **no M0-only review, merge, or waiting for manual approval**. At first real M1 playback milestone choose fast UI/native, then exact-SHA `ui + fat` for a user-test artifact (FAT includes native MSVC/CTest); no duplicate `native`. Never launch 3 similar Windows packages just for progress. The repo's approved CI dispatcher uses one-line owner PR Conversation commands and exact SHA; inspect existing run before sending. Docs-only revisions require source/diff inspection only. Real ASIO sound, routing and crackle require operator FAT and should be **PENDING** until actually witnessed.

## 7. Mandatory handoff template per agent

```text
Milestone: M0/M1/M2/M3/M4
Repo/main SHA (fresh):
PR number/head branch/exact HEAD SHA:
Changed files:
Implemented workflow + native audio proof:
Unit/integration/Chromium gates with exact CI URLs/status:
Physical FAT: PASS with evidence / PENDING / FAIL
Known defects + rollback:
No surprise autoplay/unknown physical output/second callback/source loss evidence:
Next atomic vertical milestone:
```

Do not merge docs or implementation PR into alpha.6 release branch. If reviewer/CI protection disallows merge, leave scoped PR open with a concrete status rather than force.

## 8. Binding beginner-first release gate (owner correction 2026-11-09)

Read [Zero-Friction Smart Workflow ADR](../ADR_ARSON_PLAYER_ZERO_FRICTION_SMART_WORKFLOW_20261109.md) and execute B01–B13 before any product completion claim. M0 must test `ensurePlayerFirstSound` decision/planner and idempotency. M1 must prove actual drop single file → one Play → native PCM to owned MUSIC channel → trusted MAIN, without any manual PATCH/ARM/MONITOR/confirmation/second Start or Lab. Hardware identity remains fail-closed when new/unverified, and intentional SOURCE ONLY/BUS/MAIN MUTE remains sovereign. One actual /preview/ GUI and one ASIO callback only. Browser demo screenshots alone do not certify first sound.

## 9. Fast-delivery override (2026-11-09)

[Autonomous Fast Delivery Contract](../AUTONOMOUS_FAST_DELIVERY_CONTRACT_20261109.md) overrides any historical suggestion to split M0 and M1 into separate PRs or await a review before source coding. M0 is an internal engineering fixture/first-sound planner inside the **same implementation PR** as a real M1 user-audible player; continue independent coding while quick CI runs. At a working M1 source→ChannelId→trusted MAIN native PCM + approved GUI, request one exact-SHA UI test and one Windows FAT package (not native plus FAT). Provide the owner a genuinely installable *unqualified* artifact promptly; only real ASIO equipment can complete physical FAT. No manual owner Actions/CI triggers, synthetic progress, Lab UI or permission checkbox.
