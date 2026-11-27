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
