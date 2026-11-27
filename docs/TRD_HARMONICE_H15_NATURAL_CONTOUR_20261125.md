# Harmonice H15 — pitch contour and predicted landmark continuity

Baseline: H14 429cc984daced8974dd6316a347bd4ab5a0c40f8, draft PR #889. H13 Windows run 45403738464 verified exact checkout 55d42e1d, 113/113 CTests and both packages. No H14/H15 Windows qualification yet. Owner reports some improvement but robotic and intermittent crackling remain.

## Changes
- Preserve a bounded 65% of faster microphone pitch excursions relative to a 120 ms causal note center (35 cent cap) for 3–5 harmony voices; steady off-key correction still converges to the musical target. Lead Tune and pure Doubler untouched. Duo pitch contour unchanged; predicted mono PSOLA landmark weighting also affects Duo.
- Predicted local epoch selection ranks actual extrema by amplitude and distance from the predicted phase; penalty up to 75%, only in the narrow predicted search. Wide reacquisition retains H14 behavior. No new PCM delay, threads, FFT or allocation.
- Keep explicit H14 reference for matched DSP tests. Test competing harmonics, contour retention and static convergence, 48/96 kHz 3/5 voice per-ear A/B, exact block and dry parity. Edge metrics are diagnostic, not subjective acoustic qualification.

References: https://praat.org/manual/overlap-add.html ; https://cdn.eventideaudio.com/manuals/h90/1.10.7/content/algorithms/harmonizer-plus.html ; https://www.mdpi.com/2076-3417/6/11/368 .

## Qualification
Do not claim elimination of robot/crackle from synthetic metrics. Require exact-head DSP/native CI and Windows CTest, real dry/processed vocal A/B at matched levels, per-role/stereo forensic output and actual ASIO underrun/round-trip evidence. No release or merge until qualified. No additional full FAT until scoped gates pass.
