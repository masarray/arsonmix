# P0 root cause — live MASTER L/R meter hidden by preview-only FX marker

**Reported evidence**: Windows native screenshot, CH1 green meter active, MASTER L/R bars dark, even though MASTER physical fader and stereo control render normally. Reconciled base `main@6cafdc5d094c58dcc9c3faa412dcbbb43e4574d3`; other threads own PRs #101, #102 and #103. No overwrite of their native FX reorder, CI, Orbit or fader work.

## Reproduced source-level defect

`web/bridge.js::patchMixer()` sets `track.dataset.preview="1"` on MASTER because **only the master FX/pan controls** are not routed to native DSP yet. The native injected CSS globally matches `.strip[data-preview="1"] .meter-fill` and forcibly sets `opacity:0!important`. Thus even when `src/LiveAudioEngine.h::status()` reports genuine `peakLeft` and `peakRight`, and the new ~30 FPS JavaScript LED painter advances, both MASTER LED fills are **invisible**. This is a visual-state ownership bug, not proof of broken DSP. See exact status properties `peakLeft`, `peakRight` and native pre-output peak buffers in `src/LiveAudioEngine.h`.

## Fix (two-layer guard)

1. MASTER gets a **separate** `data-fx-preview="1"`, never `data-preview="1"`; physical MASTER stereo output meter stays live. Master FX and PAN click remain disabled/preview-only, with no fabricated processing.
2. Existing generic preview CSS is narrowed to `.strip:not(.master-strip)[data-preview="1"]` for **both** the opaque LED fill gate and faded-out background track. Even accidental legacy master preview marking cannot mask real MASTER metering.
3. CH2–10 still have no native audio routing, retain the exact old preview marker and muted LED CSS. CH1 remains genuine live native input. No fake signal or meter, no change to `LiveAudioEngine.h`, JUCE telemetry, 30 FPS animation, LED/peak/clip visual geometry, HTML/CSS build or C++ realtime callback.

## Automated acceptance

- Native integration Playwright smoke uses a stub JUCE event emitter, verifies native CSS opacity for MASTER L/R 1, unrouted CH2 opacity 0, and injects a **test-only** nonzero `arMixStatus` that drives both MASTER 7px LED clips using the actual shared scheduler; no fake runtime waveform.
- Node contract enforces preview CSS scope and MASTER branch semantics.
- Full Chromium/Firefox/Orbit smoke, Windows MSVC CTest and ZIP packaging, and source/offline parity gates before merge.

## Hardware evidence boundary

User still needs to test real connected Windows audio. Genuine output peaks are zero if MONITOR disabled, MASTER muted, an output device is disconnected, a diagnostic route is capture-only or a plugin is silent. If MASTER is **visually empty after this fix while sound is audible**, inspect status fields `peakLeft`/`peakRight` and device channel map with signal check; do not generate decorative signal bars. Known issue #56 remains open for low-spec PC pointer performance until measured.
