# SonKu P9.6 — Distance Without Losing Bass & Air (PRIVATE FAT, 2026-11-16)

**Research branch, not approved sound, no public release.** Child of P9.5 PR #725 exact head b895d92c1706ab0e098bfe49447fa7b83058f865. Do not merge competing P9.5 #724 changes or alter frozen operator-approved P9.1/P9.2 checkpoint 763706a1. Preserve approved visual ellipse and P9.5 bypass/GUI fixes.

## Proven source root cause

In native P9 mode 9, with ROOM enabled, the complete direct KEMAR ear pressure is multiplied by 1/(1+0.27*(distanceMeters-1.6)), reaching **0.306x at 10m**. World-speaker LF shares the attenuation, while independent room returns can still dominate midrange. P9.5 RMS makeup / output trim do not selectively restore LF / HF. This identifies an engineering mechanism, not a listening-quality proof.

Research basis: https://pmc.ncbi.nlm.nih.gov/articles/PMC5479918/ finds atmospheric HF loss in the 1–15m range relatively small; https://pmc.ncbi.nlm.nih.gov/articles/PMC4744263/ discusses distance/DRR. The target is a **sound-reinforced concert speaker** rather than literal free-field attenuation.

## Narrow change

- OFF-by-default native flag ARMIX_SONKU_EXPERIMENTAL_P96_DISTANCE_REINFORCEMENT, requires P9.5 and P9 world source. P9.5 with flag OFF is preserved.
- P9 mode 9 only, with room ON and distance above 2m: split SAME HRTF-positioned direct ear pressure with causal 170Hz and 4.2kHz complementary first-order filters; retain more bass and air while preserving early/late wet and original source position.
- Slowly ramp distance retention over 35ms, no headlocked dry bass, no second graph, no extra FIR/delay/FFT/heap. P9.4 front/rear calibration remains multiplicative outside this band split.
- At 10m, target LF/mid/HF direct pressure gains ~0.83/0.50/0.77 before crossover rolloffs; locally tested sinus probes are approximately 0.79 at 70Hz, 0.50 at 800Hz and 0.65 at 7.5kHz. NOT verified final-output/headphone loudness.
- ROOM OFF / <=2m preserves original direct samples after transition settles; schema v11 unchanged. Native capability p96DistanceReinforcement is emitted only from the opted-in native build.
- Existing limiter (linked 0.98 sample peak), P9.5 music presence and saved sessions are unchanged.

## Acceptance before merge or release

1. 44.1/48k isolated native tests: ROOM OFF, <=2m exact direct samples, LF/MID/HF spectral energy, anti-phase, finite output; full P9.5 core P96-on source compile/render at 0/90/180/270 and 64/256 buffers.
2. Exact-head Windows JUCE/native ACK/CTest and source-provenance bundle. Do not claim CI or audio-device FAT until inspected.
3. A/B reference P9.5 versus P9.6 on headphones at 2m / 5m / 7.5m / 10m, identical input, source angle and gain; compare measured band RMS, LUFS/crest/peak protection, then loudness-match for subjective depth.
4. REJECT if distance collapses, highs become harsh, kick masks vocal, reverb becomes dry or limiter pumps. Do not claim dearVR parity without headphone evidence.
