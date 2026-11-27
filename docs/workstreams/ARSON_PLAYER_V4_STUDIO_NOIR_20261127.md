# ArSon Player V4 — Studio Noir / implementation tranche 1
Owner approved design direction 2026-11-27. Baseline canonical integration #899 d0c5de46ccd3a7b611467a3e57adc0efd3ef8787; main 379e867985bb6bab8e07297ba7a8c44925b22b2d. This is an isolated feature PR, not a second release or UI.
## Delivered source
- Local graphite/mint presentation owner in existing approved /preview/ assembly, compact header, native title/remaining/status, SVG transport, high-DPI real waveform and one click seek, cue markers, collapsed cue/transition and set management.
- Current/NEXT edit target and upcoming title derived from existing native projection; one contextual action selector per virtualized track. Preserve 38px virtualization, queue revision gestures, source level from integration, existing native actions and PCM.
- Space ignores all interactive/text controls and repeats. No changes to DSP/ASIO/routing/FX/session. No fake artwork, durations, metadata or audio.
- Peer #968 changes Choose output handler only in player JS; #970 touches shared CSS/JS outside Player. Reconcile those hunks against fresh destination before integration; do not overwrite entire shared files.
## Deferred to next tranche
- Persistent miniplayer needs approved non-obstructing placement; metadata/artwork requires real native asynchronous projection/cache and duration availability; separate queue tab and completed-history states depend on real projection. No fabricated values. Native select menu appearance and compact short-height scrolling need Chromium review.
## Qualification
- Existing player live-show test expanded for stale menu revision, NEXT label, typing safety and remaining time. Existing scoped contracts preserved. New exact-SHA CI, assembled Chromium 1366/1680/1920 and 100/125/150% scale, real Windows WebView/ASIO pending until actually run.
- Preview first per #837: no new RC, installer, full native CTest or public publish. Reconcile latest #899 and #968/#970 before integration. 

## V4.1 — Studio Noir precision finishing (27 Nov 2026)
Benchmark: [Apple Music MiniPlayer](https://support.apple.com/en-gb/guide/music/mus71d7dcfce/mac) and [queue](https://support.apple.com/guide/music/queue-your-songs-musb1e6d1c76/mac), [Roon queue](https://help.roonlabs.com/portal/en/kb/articles/the-queue), [Stage Traxx playback](https://stagetraxx.com/docs/v4/playback/) and [playlists](https://stagetraxx.com/docs/v4/library/playlists/), [djay single-deck](https://help.algoriddim.com/user-manual/djay-pro-mac/getting-started/decks), [WCAG 2.2 target size](https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum). Adopt compact single deck, separate queue view, precise controls, one contextual menu, explicit state; do not copy proprietary visuals.
- Recessed display, tactile controls, custom filled slider and value displays, compact collection switch, one shared contextual menu, search icon, native routing indicator, sequential next display with pending/queue precedence. Preserve player volume and native intent. No guessed cover art or metadata.
- Stale queue button and menu revisions guarded; no invalid external empty drag interpreted as row zero. No new DSP or routing. MiniPlayer and actual file metadata/artwork remain deferred (not present in accepted native projection).
- Exact source tests expanded; full assembled visual QA pending. No Windows installer/RC.
