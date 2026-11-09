# PRD — ArSon Player: Integrated Music, Playlist & Live Setlist
**Status:** IMPLEMENTATION-READY DESIGN (not implemented) · **Date:** 2026-11-09 · **Owner:** ArSonMix · **Baseline:** `main@975ce7c0da6c5b54e95a5d263e29a42baf1a8289`

> Product decision: A first-class **native audio source inside the ONE approved ArSonMix /preview/ mixer**, with a familiar music library, prepared setlists, safe playback queue, independent pitch/tempo, and direct ChannelId/WIRE/FX routing. Do not ship a separate MP3 window, WebAudio renderer, DAW timeline, or new mixer skin. **This document defines requirements, not shipped functionality.** Technical design: [ARD](ARD_ARSON_PLAYER_NATIVE_AUDIO_ARCHITECTURE_20261109.md). Agent execution: [implementation workpack](workstreams/ARSON_PLAYER_IMPLEMENTATION_WORKPACK_20261109.md).

## 1. Goals and user value

**Positioning:** “Load music, change its key, press PLAY, sing through your existing mixer.” One app for singer + backing track + vocal FX + live mixing + optional broadcast. Reduce external app/virtual-cable setup; retain professional patching for experts.

**Personas:**
- **Live singer / karaoke operator:** loads 10–50 backing songs, transposes -2 semitones, follows non-contiguous requests, never accidentally skips an in-progress song.
- **Worship/band show operator:** setlists divided by performance segments/breaks, song cues, manual transitions and reliable device reconnection.
- **Streamer / MC / podcast:** background music bed, short cue clips and auto-play with fades, routed into existing mixer/OBS path.
- **Expert mixer engineer:** source-only auditioning, FX insert chain, bus/aux sends, PFL, explicit output routing, detailed playback diagnostics.

**North-star journey:** open existing mixer → PLAYER → Add Files/Folder → playlist appears → choose song → set KEY -2 → PLAY → existing channel “MUSIC” meters and follows FX/FADER/WIRE to MAIN → song ends, stays stopped until operator acts. **No manual input driver loopback, no additional device wizard, no user MONITOR toggle, no audio reroute on song load.** Preserve existing verified hardware-output safety gate.

## 2. Audited competitor UX (fact / adaptation / rejected)

| Reference | Verifiable observed workflow | Adopt | Do not copy |
|---|---|---|---|
| **Stage Traxx 4.1** | Songs/Playlists/Queue separate; PLAY/NEXT/DONE markers, search, optional autoplay, sets/breaks, per-song pitch ±6, tune ±50c, speed 50–150%; explicit status of active and next song | Default **singer setlist** with clear CURRENT/NEXT, auto-stop, song override, history, configurable compact/live display | Crowded tablet navigation, lyrics-first layout in a mixer |
| **ShowOne** | Sets and Tracks; title selection does not immediately interrupt active playback; pitch semitones, tempo per track, readable show view | Safety: selecting row ≠ executing play; preflight vs destructive skip, one-click performance view | iOS-only look, automatic click without explicit routing |
| **Mixxx 2.5** | Searchable file library, ordered playlists, Auto DJ queue, preview deck, crossfade controls, missing-file recovery | Rich library/queue, explicit Play Next/Add to Queue, optional music-bed Auto DJ, power-user PFL | Two DJ decks/crossfader filling initial mixer viewport |
| **Gig Performer 5** | Streaming file list, transport, waveform, markers/loops, independent pitch/speed and BPM sync | Non-blocking stream, markers, song-level controls, advanced waveform | Feature-dense 34-control first screen, unintended global tempo sync |

**Primary sources (review date 2026-11-09):**
- Stage Traxx 4.1 [Interface Overview](https://stagetraxx.com/docs/v4/interface-overview/), [Library](https://stagetraxx.com/docs/v4/library/), [Playlists](https://stagetraxx.com/docs/v4/library/playlists/), [Pitch/Speed](https://stagetraxx.com/docs/v4/song-details/audio/).
- ShowOne [Support](https://showoneapp.com/support/) and [Features](https://showoneapp.com/).
- Mixxx 2.5 [Library/Playlists/Auto DJ/Preview](https://manual.mixxx.org/2.5/en/chapters/library).
- Gig Performer 5 [Streaming Audio File Player](https://gigperformer.com/docs_5_0/UserManualOnline/streaming-audio-file-player.html).
- **GP Mix-Lite** serves as owner-requested contextual inspiration; detailed player/key workflow is not treated as verified without authoritative documentation.

## 3. Information architecture and exact vocabulary

1. **LIBRARY** = indexed records of source audio files (not duplicating audio content); searchable; metadata, last-used, availability; file identity survives rename/relink. Removing from playlist never deletes audio file.
2. **PLAYLIST / SETLIST** = durable ordered collection of `SongRef` entries for a show. Song may appear in multiple setlists with different *setlist-entry overrides*. Support segments (“Set 1”, “Break”, “Set 2”) as later enhancement.
3. **QUEUE** = ephemeral ordered performance intent; supports “Play Next”, “Add to Queue”, reorder, remove and “Clear Upcoming”. Queue does not silently rewrite source playlist.
4. **PLAYER A** = active native transport engine and audio source; CURRENT is playing/paused/stopped loaded song; NEXT is **prepared candidate**, never implicitly audible.
5. **PLAYER B** = future optional second audio source for seamless crossfade/preview. Design stable IDs and migration now; do not expose a nonfunctional B control in MVP.
6. **CHANNEL** = existing mixer ChannelId source / FX / fader / routing. “MUSIC PLAYER” is a *source*, not a new hardware device and not inherently a new mixer/channel role.
7. **PITCH/KEY** = semitone transposition of audio independent of time. “Tempo/Speed” = playback time rate independent of musical pitch. Original key metadata is **not** automatically known unless analyzed or manually entered.
8. **Modes:** `PERFORMANCE` (default: STOP AFTER SONG), `CONTINUOUS` (autoplay selected), `ADVANCED` (more controls, not different engine). Explicitly show end behavior in transport; setlist may override by song.

## 4. Operator workflow contracts

**UX-01 First-use, 3-step happy path.** Existing mixer is running. Click `Player` in the approved console toolbar → compact drawer says `Add Music` → select multiple files or folder → tracks appear in library and a new “Untitled Setlist” with their initial order. Selecting any song highlights its row and can load safely when transport is stopped. Click PLAY; app chooses/creates a *verified* dedicated MUSIC channel source and connects to the existing MAIN only if that route is already explicitly configured/accepted or the user confirms a proposed route. Never bypass output safety. If hardware is not ready, load/setlist still works; show `Output not ready`, PLAY does not claim audible success.

**UX-02 Normal performance.** Compact transport shows Current title, Next title, big legible PLAY/PAUSE/STOP, progress + elapsed/remaining, KEY, TEMPO and explicit `Stop after song` indicator. Space toggles play/pause only when Player panel has keyboard focus (avoid clashing with mixer). On track end, STOP and display `Ready: <next>`. NEXT button or an explicit row `Load Next` moves to selected next track; no auto-next by default.

**UX-03 Selection safety.** Single click row = SELECT/PREPARE NEXT, **not** seek/play/switch CURRENT. Double click while playback active = prompt/explicit `Play Now` action (never hidden unconditional destructive switch). Context menu: Play Now (warn if current playing), Play Next, Add to Queue, Edit Song, Locate Missing File, Remove from Setlist; `Delete from disk` is not available in live UI. A pending NEXT can change while CURRENT continues unchanged.

**UX-04 Song-specific pitch.** Adjust KEY in integer semitones `-6…+6` (step 1; source key display optional), RESET to 0. Apply to active playback via safe smoothing/buffer generation without pops or tempo change; per-song saved value persists. Optional +/−50 cent fine tuning in Advanced after DSP gate. Show e.g. `Key -2 st`; do not mislabel absolute key if original key not verified.

**UX-05 Speed.** TEMPO shown as `100%` by default with +/- and reset, `50…150%` in Advanced after stress tests; practical primary UI quick range `80…120%`. An optional original BPM field may compute displayed effective BPM; **no automatic global FX BPM sync** without provenance and explicit enabling. Speed change keeps pitch constant.

**UX-06 Setlist editing.** Create, rename, duplicate, reorder via drag + keyboard Move Up/Down; bulk add files, add from library, search/filter; same song may occur repeatedly; persistent ordered entries have distinct IDs. Switch setlists while playing only changes browsing/queue, never CURRENT. Save and restore without losing song settings.

**UX-07 Cue/markers.** Advanced: start/end trims, loop A/B, named cue markers, single click navigate when stopped; seeking during playback is guarded by a configurable `Live Seek Lock` enabled by default. Playing without a cue starts at song's accepted start marker. Song end fades to silence and advances readiness, not actual hardware route.

**UX-08 Auto / crossfade.** `Continuous` opt-in: preload NEXT before EOF; when auto enabled, next begins according to configured gap or fade. True overlapping crossfade requires TWO prepared audio streams mixed in ONE native renderer, not overlapping file load or swapping active reader inside callback. No autoplay across a “Break” segment or invalid/missing song unless explicitly configured. Auto-mode status unmistakable.

**UX-09 Mixer channel.** `IN` picker offers `PLAYER A (stereo)` among *source kinds*, not ASIO input ordinals. Default logical channel format stereo, optional mono fold-down via existing rules. SOURCE ONLY (no output WIRE) must show source meter but MAIN silent. Changing source or WIRE must not reset player transport, song state, FX, channel ID, or hardware device. Multiple consumers cannot drain the same FIFO independently; one native producer, shared per-block tap, deterministic fan-out.

**UX-10 Monitoring / OBS.** Playback follows existing physical MAIN + secondary stream, existing monitor/cue routing and policies. PFL/preview must never leak into MAIN/OBS, except explicitly connected. Keep mic ASIO low-latency priority.

**UX-11 Missing/corrupt files.** A missing file is flagged `Locate` but retains order/settings. Relink via explicit user file-picker with file fingerprint check; stale/corrupt file must never silently play a different song. During show, skip unavailable NEXT with visible warning only according to selected fail policy; no unbounded recovery loops or popups on audio callback.

**UX-12 Offline & portability.** Works without network, supports relative paths in portable/show folders, optional explicit “Collect Media for Show” copy with progress/space check and source copyright responsibility. Never embed the entire media file in mixer scene document. Project export can reference media, with clear missing-media status after moving PCs.

## 5. GUI design specification (ONE canonical /preview/)

**Placement:** add a small `Player` toolbar toggle to the existing approved command bar. Closed: *thin* persistent mini transport strip (PLAY/PAUSE, CURRENT, elapsed, NEXT, KEY) integrated unobtrusively without changing MAIN pinned-right layout. Open: 360–420 px **non-modal left drawer**, never covering pinned MAIN/faders by default; collapsible to mini transport. At desktop width 1366 px, use overlay rather than shrink channel strips; user may move/resize advanced window. **Do not add an alternative mixer DOM**. Native and public Pages must use same assembled /preview/ HTML; browser preview explicitly `DEMO / NO AUDIO` if no native bridge.

**Compact drawer 3-level hierarchy:**
- Header: `ARSON PLAYER` / setlist selector / `+ Music` / collapse / More.
- Current transport: title + artist, 1 waveform/progress, remaining timer, tactile Play/Pause (min 40 px hit target), Stop, Next. KEY and TEMPO have matching compact numeric control style (not disparate knobs). Status chips text: `STOP AFTER SONG` (default) or `AUTO NEXT`.
- Below: tabs `SETLIST | LIBRARY | QUEUE`; search field, virtualized dense song rows, columns Name | Key delta | Duration, leading tiny PLAY/NEXT/DONE markers. NEXT highlighted distinct from SELECT and CURRENT; metadata is readable without hovering.

**Advanced panel (not MVP default):** waveform with A/B loop + named markers, pitch cents, tempo, fade in/out, gain, key metadata, routing readback, optional Player B/preview. Keep existing premium charcoal/cyan/green hardware aesthetic, low visual noise, 12–14px body text and compact spacing; no oversized cards, icons without labels or misleading fake LEDs. Buttons have consistent tactile idle/hover/pressed/focus states and do not jump in size.

**Interaction and accessibility:** keyboard/real native context menus match approved app (do not show Chromium context menu); Tab/arrow/Enter navigation; visible keyboard focus, screen reader labels, touch targets min 36–40px, tooltips as supplements, pointer drag reorder plus keyboard alternative, reduced-motion support; state changes require readback, no optimistic `PLAYING` on rejected native intent. Responsive screenshots at 1366/1440/1680/1920 widths and 100–150% DPI.

**Reference low fidelity (layout, not a new skin):**

```text
Approved /preview/ toolbar:  ...   [ PLAYER ]  ... [Pinned MAIN]
Miniplayer (always): [Play] Song title · 01:34 / 03:45   Key -2   Next: Song B
Open LEFT PLAYER drawer
┌ ARSON PLAYER     Setlist: Tonight ▾    +Music   × ┐
│ CURRENT   Song A — Artist             01:34 / 03:45 │
│ ━━━━━━━●━━━━━━━━━━━━━━━━━                          │
│ [Prev] [STOP] [  PLAY / PAUSE  ] [Next]            │
│ KEY [-]  -2 st [+] [Reset]  TEMPO [-]100%[+]      │
│ [STOP AFTER SONG ▼]     ROUTED → MUSIC CH (ACK)    │
│ [Setlist] [Library] [Queue]    Search...           │
│ ▶ Song A      -2      03:45     CURRENT            │
│ → Song B       0      04:02     NEXT               │
│ ✓ Song C      +1      03:10     DONE               │
└──────────────────────────────────────────────────────┘
```

## 6. Feature priority & non-goals

**P0 product milestone (native usable):** source kind, one Deck A, decoder + prepared playback, library/multi-import, basic persistent setlist, manual queue, source channel routing, Play/Pause/Stop/Next, seek when stopped, song-level KEY ±6 & speed 80–120% after DSP proven, current/next UI/ACK, fail-closed file handling, source and output meters.

**P1:** full tested speed 50–150%, tune ±50c, library indexing/rescan, play history, markers/loop/trims, fade and preloading, configurable Auto Next, song/end behavior, M3U import/export & collect-media flow, optional PFL preview. User-visible controls must not ship without actual native support.

**P2:** two simultaneous prepared decks + true crossfade, smart playlist, BPM/key analysis with confidence, external MIDI/hotkeys, multi-stem backing (click/stems to separate mixer source IDs), global tempo sync only if compatible native transport.

**Explicit non-goals:** multitrack DAW timeline, sample editor, streaming/DRM or browser media API, cloud account, downloading copyrighted music, automatic cue click routing, replacing primary mixer routing, converting singer mic to processed pitch/tempo path.

## 7. Acceptance matrix and UX targets

| ID | Evidence/acceptance |
|---|---|
| AC-01 | On a clean show: add 20 mixed WAV/MP3/FLAC files, build a saved setlist and play on PLAYER A through a live native ChannelId into MAIN; meter is **native PCM**, not JS animation |
| AC-02 | While A plays, select/change NEXT/another playlist/queue: audio sample progression, live mic, fx & routing **unchanged** |
| AC-03 | End of song in default mode: silence & STOP with NEXT ready; never surprises singer with autoplay |
| AC-04 | Pitch -2, +3, reset and speed 90%, 110% produce measured expected pitch/length changes without mic pitch shift, audible clicks or graph restarts |
| AC-05 | Physical IN → vocal channel & PLAYER A → music channel concurrently; bus/fx fader/solo/mute/MAIN and optional secondary stream all behave per accepted graph |
| AC-06 | SOURCE ONLY shows player/channel meter while MAIN silent; accepted WIRE to MAIN restores sound without reloading song |
| AC-07 | Missing/corrupt/unsupported file, disk stall, invalid seek & stale generation: bounded failure state, never block ASIO callback, preserve valid graph and last loaded/queued records |
| AC-08 | Save/restart, portable path move, device close/reopen, 44.1/48/96k, block 64/128/256: no silent source remap or false PLAYING state; explicit output safety applies |
| AC-09 | Native UI events correlated with native requestId, playerGeneration and deviceEpoch/graphRevision as applicable; no false ACK or demo playing state |
| AC-10 | Real keyboard/touch/1366-1920 layouts, 150% DPI, click-right menu, visual QA, 30-min mixed music + microphone ASIO soak and independent recording verified; operator can use stage controls without opening Audio Setup |
| AC-11 | Same track may appear in multiple sets without duplicate audio files; setlist-specific overrides do not unexpectedly overwrite global song defaults |
| AC-12 | Automated low-overhead callback tests show **zero locks, allocations, synchronous disk reads, plugin instantiation or JS calls** on realtime thread; underruns counted and isolated |

**Engineering performance budgets (proposed targets; verify on representative machines):** music startup from *already prepared* track p95 <100ms; pause/stop and mute ramps ≤20ms after accepted command; background media buffer start at 1–3s and adaptive refill with bounded memory; GUI readback ≤10 Hz while playing and events on transitions; ASIO 64/128/256 stability with existing mic engine and selected FX; no additional capture→monitor latency when player is enabled; no blocking metadata/waveform extraction on audio/UI threads. Do not claim compliance without measurements.

## 8. Release gates / ownership

- Existing engine P0 routing recovery [issue #412](https://github.com/masarray/arsonmix/issues/412), dynamic channel [#192](https://github.com/masarray/arsonmix/issues/192), one approved GUI [#389](https://github.com/masarray/arsonmix/issues/389) and hardware crackle [#15](https://github.com/masarray/arsonmix/issues/15) are **not** superseded by player.
- Native renderer only; control intent → native validated command → accepted state → immutable graph publication → actual callback → ACK/readback.
- Existing release branch alpha.6 and ongoing Vocal Throw/Harmony PRs must not be rewritten/merged as a side effect.
- New per-song tempo data may later integrate with Vocal Throw BPM; no hidden dependency on #474/#476, no automatic replacement of manual/TAP fallback.
- No CI for this docs-only architecture PR; future code milestones use fast scoped tests, then selective exact-SHA `ui`/`native`/`fat` as specified by [AGENTS.md](../AGENTS.md). Physical FAT stays PENDING until performed.
