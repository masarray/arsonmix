# Agent Workpack — ArSon Player Native Music / Setlist
**Date:** 2026-11-09 · **Scope:** executable vertical implementation plan from [PRD](../PRD_ARSON_PLAYER_LIVE_SETLIST_UX_20261109.md) and [ARD](../ARD_ARSON_PLAYER_NATIVE_AUDIO_ARCHITECTURE_20261109.md) · **Status:** PLANNED, NOT IMPLEMENTED.

> **Read the canonical repository [AGENTS.md](../../AGENTS.md) and [Smart CI](../SMART_CI_RUNNER_WORKFLOW.md) first.** Current documents are architectural handoff, not permission to merge untested code, overwrite another agent or publish an alpha release. Must fetch live main, active PRs, latest approved GUI assembly and exact runtime PCM before editing; do not rely on checkpoint SHA as perpetual authority.

## 0. One-sentence delivery contract

**One accepted native PLAYER A source**, serving predecoded/pitch-adjusted PCM to an existing ChannelId mixer strip in the ONE JUCE callback, with real waveform-independent transport/playlists and reliable native ACK/readback in exactly `/preview/`; no extra audio engine, hidden manual patch wizard or fake browser playing state.

## 1. Dependencies and commit strategy

- **Foundation:** engine source binding must distinguish `physical` from `playerDeck`; compiler/graph migration must preserve P0 hardware input/WIRE/FX/MAIN behavior and sample-accurate source fan-out.
- **UI authority:** native compiled asset is derived from approved `/preview/` assembler; do not work in old `web/index.html`, ship a second player website, or overwrite user-approved mixer artwork.
- **Concurrency:** PR #468 release alpha.6, #467 Harmony, and #469/#474/#476/#479 Vocal Throw are out-of-scope. Audit current status again; no cherry-pick or speculative conflict resolution. Preserve GUI/native product only after on-main reconciliation.
- **Suggested source branch:** `feature/arson-player-native-source-m1-YYYYMMDD` from fresh main; integrate one meaningful vertical milestone and tests per PR. Next milestones can follow cleanly without fragmenting dozens of parallel PRs. Do not create a duplicate active integration branch if player implementation work already exists.
- **No backend bypass:** no unverified logical wire readback, no demo native PCM, no auto-pick physical output, no silent ASIO device restart or Monitor toggles.

## 2. Milestone / independently testable vertical slices

### M0 — Architecture spike and exact baseline (no UI feature promise)
**Goal:** capture actual current render path and make player integration design non-speculative.

Tasks:
1. Re-fetch `main`, README, AGENTS, `src/Main.cpp` (JUCE callback & mixer command), `src/LiveAudioEngine.h`, `engine/core/DynamicPreparedMixerBlock.h`, `engine/core/DynamicNativeSessionPublisher.h`, `engine/core/DynamicChannelRoster.h`, `src/NativeMixerSessionDocument.h`, `integration/ApprovedNativeControlIntent.h`, `web/approved-native-bridge.js`, GUI builder scripts and relevant tests.
2. Trace and document observed `PCM producer → graph compiler → render publisher → callback → device` path and any unused staging code; test baseline Hardware IN → CH → FX → WIRE → MAIN, SOURCE ONLY, independent secondary output, device epoch restart.
3. Build a minimal deterministic offline virtual-source fixture to prove accepted graph can process a synthetic stereo stream on a ChannelId **without** fake physical pin indices. Verify legacy physical inputs bitwise unaffected.
4. Choose strong typed `SourceBinding` and callback producer contract; prove two ChannelId consumers of same deck advance one stream once. Produce exact diff/CTest.

**Exit gate:** a deterministic native PCM sample fixture passes on existing graph, virtual source does not require ASIO pin, no new callback, no changed GUI or device. If P0 routing fails, log the blocker and fix root cause rather than building around it.

### M1 — One truthful player, library & setlist (meaningful first product milestone)
**Goal:** usable integrated player with WAV/MP3/FLAC verified formats and manual track transition, **no pitch GUI promise before DSP**.

Native:
1. Introduce `engine/player/PlayerContracts.h`, `PlayerTransportState.h`, `PreparedPlayerSource.h`, `SourceBindingPlan.h` (names may change if a better existing owner exists).
2. Implement native `src/NativePlayerController.h`, decoder worker, bounded PCM ring, generation-safe play/pause/stop/load/seek/EOF, rate conversion and short ramps; integrate in **one existing mixer callback** via `playerDeck` SourceBinding. Create no second JUCE AudioDeviceManager.
3. Establish thread and fault tests including decoder stall, rapid STOP/LOAD and rejected source selection; ensure no heap, disk, locks, waits, window, logs, plugin operation in callback.
4. Add `PlayerMediaStore` versioned portable Library/Setlist storage and relink safety; default STOP_AFTER_SONG, separate next selection, never delete a source file.
5. Add proposed player intents/snapshot to existing native WebView bridge with expected revisions / deck generation and no false-playing in demo.

UI:
6. Extend the **approved Pages source and assembler** with toolbar `Player`, mini transport + compact non-modal left drawer, tabs SETLIST/LIBRARY/QUEUE, dense rows & +Music; simple transport current/next/ACK and intuitive error labels. Keep MAIN visible and one renderer.
7. Use existing CSS token/premium hardware styles. Test responsive layout + pointer and native context menu; no bulky cards, giant bold fonts, or custom Chrome right click.

**Exit gate:** 20-file import, save/reopen, manual NEXT, play/stop, 44.1/48k and SOURCE ONLY vs MAIN PCM evidence; progress and underruns read from actual render; mic ASIO unaffected; no demo audio masquerade. If PITCH/SPEED not ready, hide/disable them with explicit “coming after DSP QA”, never render inert working-looking controls.

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
- [ ] Every transport and player route action either ACKs accepted native state or rejects with specific fault.
- [ ] SOURCE ONLY + dynamic bus/WIRE + VST3 + fader/solo/mute unchanged from accepted engine.
- [ ] Dead or missing media does not block hardware and does not delete playlist entries.
- [ ] Existing main mixer available and correct when Player pane is hidden.
- [ ] Library/Setlist/Queue state consistent, persisted and replayed only where safe; restart never auto-PLAY/arm.
- [ ] Relative paths and exact source identity verified; no playback of an unrelated relinked file.
- [ ] Audio block is allocation-free, no locks/disk/FFT/decoder/JS and cannot let worker starvation stall ASIO.
- [ ] Visually fits user-approved `/preview/`, no huge cards, giant font, crowding, or displaced MAIN fader.
- [ ] Relevant Linux/Windows/macOS, browser and real hardware tests logged against **exact commit SHA**.
- [ ] Build package has exact source provenance and clean install; actual physical FAT separately marked PASS/PENDING.
- [ ] PR states changed files, test evidence, constraints/limitations, branch, HEAD, main SHA, next milestone; no fake shipped player claims.

## 6. Workflow/CI cost policy

M0 design/tests: add unit fixture, fast test only. M1/M2 C++ and JS: fast native+UI first, one `ui` visual run and one `native` lane (or `fat` in place of native if user needs installer) after exact-sha quick gates and duplicate check. Never launch 3 similar Windows packages just for progress. The repo's approved CI dispatcher uses one-line owner PR Conversation commands and exact SHA; inspect existing run before sending. Docs-only revisions require source/diff inspection only. Real ASIO sound, routing and crackle require operator FAT and should be **PENDING** until actually witnessed.

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
No auto-ARM/second callback/source loss evidence:
Next atomic vertical milestone:
```

Do not merge docs or implementation PR into alpha.6 release branch. If reviewer/CI protection disallows merge, leave scoped PR open with a concrete status rather than force.
