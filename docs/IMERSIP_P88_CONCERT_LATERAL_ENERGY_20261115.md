# ImerSip — Concert Stage Lateral Early Energy (2026-11-15)

## Owner objective and prior state
User says current SonKu P8.7-adapted ImerSip is not yet luxurious, celebratory and genuinely immersive. At commit 67b1f7f, the measured direct KEMAR front speaker path, 3.9k P8.7 musical air and real FDN LP175 musical body work at 44.1/48k. These features cannot on their own guarantee headphone externalization (headphone-specific HRTFs, no head-tracking).

## Research reasoning
2026 frontal externalization study reports level and early-reflection adjustment improve frontal virtual source externalization (https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/). True source width correlates more to appropriately directional EARLY lateral energy; envelopment correlates more with late diffuse energy (see https://doi.org/10.3390/app12062823 and https://doi.org/10.1038/s44384-025-00027-4). Unbounded late reverberation risks making speakers indistinct and appearing above/around the head; simply adding wet tails is wrong.

## Scoped implementation
- One JUCE/native P8.2/P8.4 core, existing P8.6 six-wall horizontal focus and P8.7 air/body. NO stereo Haas, fake reverb, additional BRIR, worker, 2nd source, FFT, allocations or new public render-mode.
- ImerSipConcertStage.h makes only a 250–3800Hz 1-pole bounded lateral band from already spatialized directional early reflections. Maximum additional 70% of this MEASURED early-side band scales with current validated IMMERSION * VENUE SPACE * STAGE WIDTH. No extra signal at 0% mix, 0% venue space or 0% stage width. Gains slew 25ms.
- The additional signal is exactly L=-R, preserving existing dry MID, vocal mono fold-down and the original concert bass. It does not enhance late HF/diffuse FDN reverb, and the existing optional-enhancement headroom gate and global linked limiter remain authoritative.
- Explicit test-only A/B constructor lets tests measure the new stage versus same P8.7 core with no new lateral contribution, with exact 64/128/8192 audio chunk parity. No .smix v2 state layout change. UI knobs remain familiar and source-authoritative.

## Qualification before owner FAT
44.1/48k: true binaural direct HRIR, P8.7-enabled output, independent 250-3800Hz lateral test, exact dual-ear mid cancellation, source-stage A/B PCM evidence, 0% space/width boundary, old mono vocals, anti-phase, real six-wall ER, bass 40–220Hz, high-peaks and chunk parity. Full Windows JUCE + PE/provenance and approved portable ZIP on SAME SHA needed before sharing a new test binary. Never claim more immersive just from PCM difference; level-matched physical headphones decide.
