# PRD — ArSon Player: Integrated Music, Playlist & Live Setlist
**Status:** IMPLEMENTATION-READY DESIGN (not implemented) · **Date:** 2026-11-09 · **Owner:** ArSonMix · **Baseline:** `main@975ce7c0da6c5b54e95a5d263e29a42baf1a8289`

> Product decision (revised 2026-11-09): A first-class native player integrated in ONE approved /preview/ mixer. **New binding decision:** [Zero-Friction Smart Workflow ADR](ADR_ARSON_PLAYER_ZERO_FRICTION_SMART_WORKFLOW_20261109.md). Drop/select music → Play → the app prepares one owned MUSIC ChannelId, PLAYER A source and default WIRE→already verified MAIN **automatically**, with no user approval checkbox, ARM, MONITOR, Lab, manual route wizard or second Start. Never silently change physical output or overwrite user-authored SOURCE ONLY/BUS routing. This is a design, not implementation.

## 1. Goals and user value

**Positioning:** “Load music, change its key, press PLAY, sing through your existing mixer.” One app for singer + backing track + vocal FX + live mixing + optional broadcast. Reduce external app/virtual-cable setup; retain professional patching for experts.

**Personas:**
- **Live singer / karaoke operator:** loads 10–50 backing songs, transposes -2 semitones, follows non-contiguous requests, never accidentally skips an in-progress song.
- **Worship/band show operator:** setlists divided by performance segments/breaks, song cues, manual transitions and reliable device reconnection.
- **Streamer / MC / podcast:** background music bed, short cue clips and auto-play with fades, routed into existing mixer/OBS path.
- **Expert mixer engineer:** source-only auditioning, FX insert chain, bus/aux sends, PFL, explicit output routing, detailed playback diagnostics.

**North-star journey:** Drop song (or Player → Add Music), select if necessary, press PLAY once, hear music through the currently verified MAIN. The app automatically creates/reuses one MUSIC source/channel and its factory default trusted MAIN WIRE, prebuffers and plays with native ACK; imported multiple files become a ready setlist, and KEY/TEMPO adjustments are optional. No engineer-only setup. Existing user-custom routes, MAIN mute and actual device-identity safety are honored.

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
8. **One Player, no modes:** default stops after song; a small optional Auto Next control supports continuous music; contextual More/Details reveals professional functions without lab/configuration mode or setup gate.

## 4. Operator workflow contracts

**UX-01 Two-action first sound.** Drop 1 music file (or choose via native file picker), then click PLAY: owned MUSIC ChannelId+PLAYER A+factory WIRE→existing verified MAIN are prepared and committed automatically, with no checkbox, approval, PATCH, MONITOR, ARM, APPLY or second Start. For many files, the app creates and saves an ordered setlist automatically; first song is READY. If physical output genuinely differs/unverified, present ONE contextual Choose output action and fulfill the original Play request while still current. No silent new-speaker selection or MAIN unmute.

**UX-02 Normal performance.** Compact transport shows Current title, Next title, big legible PLAY/PAUSE/STOP, progress + elapsed/remaining, KEY, TEMPO and explicit `Stop after song` indicator. Space toggles play/pause only when Player panel has keyboard focus (avoid clashing with mixer). On track end, STOP and display `Ready: <next>`. NEXT button or an explicit row `Load Next` moves to selected next track; no auto-next by default.

**UX-03 Direct familiar selection.** Single-click a row while stopped makes it READY; single-click while another song plays selects/preloads NEXT without interruption. Double-click/Play Now transitions directly via click-free native fade, without warning or consent dialog. Context actions are Play Now, Play Next, Add to Queue, Edit, Locate, Remove; Remove from Setlist supports Undo and never deletes disk media.

**UX-04 Song-specific pitch.** Adjust KEY in integer semitones `-6…+6` (step 1; source key display optional), RESET to 0. Apply to active playback via safe smoothing/buffer generation without pops or tempo change; per-song saved value persists. Optional +/−50 cent fine tuning in Advanced after DSP gate. Show e.g. `Key -2 st`; do not mislabel absolute key if original key not verified.

**UX-05 Speed.** TEMPO shown as `100%` by default with +/- and reset, `50…150%` in Advanced after stress tests; practical primary UI quick range `80…120%`. An optional original BPM field may compute displayed effective BPM; **no automatic global FX BPM sync** without provenance and explicit enabling. Speed change keeps pitch constant.

**UX-06 Setlist editing.** Create, rename, duplicate, reorder via drag + keyboard Move Up/Down; bulk add files, add from library, search/filter; same song may occur repeatedly; persistent ordered entries have distinct IDs. Switch setlists while playing only changes browsing/queue, never CURRENT. Save and restore without losing song settings.

**UX-07 Contextual cue/markers.** Start/end trims, A/B loop and cues in song Details. A simple click on progress display does not accidentally seek while PLAYING; purposeful drag on waveform/seek handle or keyboard seek is accepted with audio ramp. No user-facing Live Seek Lock checkbox; no modal confirmation.

**UX-08 Auto Next/crossfade.** Auto Next is a single optional inline control, OFF by default; no Performance/Continuous mode chooser. Default singer playback stops after song with NEXT ready; break boundaries respected. Preload NEXT off-RT. Real overlap/crossfade requires future second prepared deck in ONE native callback, not hidden extra player engine.

**UX-09 Mixer channel.** `IN` picker offers `PLAYER A (stereo)` among *source kinds*, not ASIO input ordinals. Default logical channel format stereo, optional mono fold-down via existing rules. SOURCE ONLY (no output WIRE) must show source meter but MAIN silent. Changing source or WIRE must not reset player transport, song state, FX, channel ID, or hardware device. Multiple consumers cannot drain the same FIFO independently; one native producer, shared per-block tap, deterministic fan-out.

**UX-10 Monitoring / OBS.** Playback follows existing physical MAIN + secondary stream, existing monitor/cue routing and policies. PFL/preview must never leak into MAIN/OBS, except explicitly connected. Keep mic ASIO low-latency priority.

**UX-11 Missing/corrupt files.** A missing file is flagged `Locate` but retains order/settings. Relink via explicit user file-picker with file fingerprint check; stale/corrupt file must never silently play a different song. During show, skip unavailable NEXT with visible warning only according to selected fail policy; no unbounded recovery loops or popups on audio callback.

**UX-12 Offline & portability.** Works without network, supports relative paths in portable/show folders, optional explicit “Collect Media for Show” copy with progress/space check and source copyright responsibility. Never embed the entire media file in mixer scene document. Project export can reference media, with clear missing-media status after moving PCs.

## 5. GUI design specification (ONE canonical /preview/)

**Placement:** add a small `Player` toolbar toggle to the existing approved command bar. Closed: *thin* persistent mini transport strip (PLAY/PAUSE, CURRENT, elapsed, NEXT, KEY) integrated unobtrusively without changing MAIN pinned-right layout. Open: 360–420 px **non-modal left drawer**, never covering pinned MAIN/faders by default; collapsible to mini transport. At desktop width 1366 px, use overlay rather than shrink channel strips; user may move/resize advanced window. **Do not add an alternative mixer DOM**. Native and public Pages must use same assembled /preview/ HTML; browser preview explicitly `DEMO / NO AUDIO` if no native bridge.

**Compact drawer 3-level hierarchy:**
- Header: `ARSON PLAYER` / setlist selector / `+ Music` / collapse / More.
- Current transport: compact title, progress, remaining time, tactile PLAY/PAUSE/STOP/NEXT and matching KEY/TEMPO numeric controls **only when DSP works**. Small Stop After Song / Auto Next label; no route config, consent or Lab in first-screen UI.
- Below: show current setlist directly, with dense Name | Key delta | Duration rows, clear CURRENT/NEXT/DONE. Library and Queue are secondary tabs/links, not first-use decisions. Empty state shows one Add Music action; one-file import does not require a setlist.

**Contextual Details, not a mode:** waveform, loop/cues, cents, tempo, fade, gain, routing readback and future Deck B via compact More/Song Details disclosure. No separate user-facing Advanced/Lab mode or setup wizard. Keep approved dark/cyan premium hardware aesthetic, compact spacing/body 12–14px, identical tactile pressed/idle size and readable non-hover actions.

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

**P0 product milestone (native usable):** first Play automatically provisions one owned MUSIC ChannelId/PLAYER A source/default WIRE→already verified MAIN, native PCM/readback, import/drop single/folder, automatic persistent setlist, safe manual current/next queue, PLAY/PAUSE/STOP/NEXT, source meter, robust error handling, no confirmation/ARM/MONITOR/Lab. Expose KEY ±6 and speed 80–120% only after native DSP passes acoustic/CPU QA. Beginner acceptance B01–B13 in Zero-Friction ADR is mandatory.

**P1:** full tested speed 50–150%, tune ±50c, library indexing/rescan, play history, markers/loop/trims, fade and preloading, configurable Auto Next, song/end behavior, M3U import/export & collect-media flow, optional PFL preview. User-visible controls must not ship without actual native support.

**P2:** two simultaneous prepared decks + true crossfade, smart playlist, BPM/key analysis with confidence, external MIDI/hotkeys, multi-stem backing (click/stems to separate mixer source IDs), global tempo sync only if compatible native transport.

**Explicit non-goals:** multitrack DAW timeline, sample editor, streaming/DRM or browser media API, cloud account, downloading copyrighted music, automatic cue click routing, replacing primary mixer routing, converting singer mic to processed pitch/tempo path.

## 7. Acceptance matrix and UX targets

| ID | Evidence/acceptance |
|---|---|
| AC-01 | Drop 20 files → setlist automatically created. Select one song → single Play action automatically creates/reuses player-owned MUSIC ChannelId and trusted MAIN WIRE, yields **real PCM and audible output** without PATCH/ARM/MONITOR/confirmation or second Start |
| AC-02 | While A plays, select/change NEXT/another playlist/queue: audio sample progression, live mic, fx & routing **unchanged** |
| AC-03 | End of song in default mode: silence & STOP with NEXT ready; never surprises singer with autoplay |
| AC-04 | Pitch -2, +3, reset and speed 90%, 110% produce measured expected pitch/length changes without mic pitch shift, audible clicks or graph restarts |
| AC-05 | Physical IN → vocal channel & PLAYER A → music channel concurrently; bus/fx fader/solo/mute/MAIN and optional secondary stream all behave per accepted graph |
| AC-06 | SOURCE ONLY shows player/channel meter while MAIN silent; accepted WIRE to MAIN restores sound without reloading song |
| AC-07 | Missing/corrupt/unsupported file, disk stall, invalid seek & stale generation: bounded failure state, never block ASIO callback, preserve valid graph and last loaded/queued records |
| AC-08 | Save/restart, portable path move, device close/reopen, 44.1/48/96k, block 64/128/256: no silent source remap or false PLAYING state; explicit output safety applies |
| AC-09 | Native UI events correlated with native requestId, playerGeneration and deviceEpoch/graphRevision as applicable; no false ACK or demo playing state |
| AC-10 | At 1366–1920 widths/150% DPI, keyboard/touch and native context menus work; physical ASIO+mic/OBS FAT performed. For already verified output, ordinary player needs NO Audio Setup or route wizard. Unknown physical output requires only a single inline Choose output action, not consent checkbox or extra START |
| AC-11 | Same track may appear in multiple sets without duplicate audio files; setlist-specific overrides do not unexpectedly overwrite global song defaults |
| AC-12 | Automated low-overhead callback tests show **zero locks, allocations, synchronous disk reads, plugin instantiation or JS calls** on realtime thread; underruns counted and isolated |

**Engineering performance budgets (proposed targets; verify on representative machines):** music startup from *already prepared* track p95 <100ms; pause/stop and mute ramps ≤20ms after accepted command; background media buffer start at 1–3s and adaptive refill with bounded memory; GUI readback ≤10 Hz while playing and events on transitions; ASIO 64/128/256 stability with existing mic engine and selected FX; no additional capture→monitor latency when player is enabled; no blocking metadata/waveform extraction on audio/UI threads. Do not claim compliance without measurements.

## 8. Release gates / ownership

- Existing engine P0 routing recovery [issue #412](https://github.com/masarray/arsonmix/issues/412), dynamic channel [#192](https://github.com/masarray/arsonmix/issues/192), one approved GUI [#389](https://github.com/masarray/arsonmix/issues/389) and hardware crackle [#15](https://github.com/masarray/arsonmix/issues/15) are **not** superseded by player.
- Native renderer only; control intent → native validated command → accepted state → immutable graph publication → actual callback → ACK/readback.
- Existing release branch alpha.6 and ongoing Vocal Throw/Harmony PRs must not be rewritten/merged as a side effect.
- New per-song tempo data may later integrate with Vocal Throw BPM; no hidden dependency on #474/#476, no automatic replacement of manual/TAP fallback.
- No CI for this docs-only architecture PR; future code milestones use fast scoped tests, then selective exact-SHA `ui`/`native`/`fat` as specified by [AGENTS.md](../AGENTS.md). Physical FAT stays PENDING until performed.

## Operator-first mandatory amendment — 2026-11-09

[Smart Zero-Friction Workflow ADR](ADR_ARSON_PLAYER_ZERO_FRICTION_SMART_WORKFLOW_20261109.md), requirements B01–B13, supersedes any older player wording that demands route confirmation, separate user ARM/MONITOR/Apply/Start, Lab mode, or a technical acceptance checkbox. One explicit Play on the *same previously verified output* is sufficient for internal safe prepare/activate; unknown/different endpoint requires one contextual physical output selection. Factory-owned MUSIC→MAIN auto-link is **not** permission to undo user-authored SOURCE ONLY, BUS, MAIN MUTE or existing FX/ChannelId topology. Native ACK and one PCM callback remain mandatory.
