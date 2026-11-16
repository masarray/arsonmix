# SonKu P9.9 — Musical Vocal Depth / Wide Instrument Layers (2026-11-16)

## Approved checkpoints and strict boundaries

- **Venue Mega Space is frozen** at P9.7 SHA `fda0eef669e0a59575c489e149662c1bf878c998` on `checkpoint/sonku-p97-venue-mega-space-approved-20261116`. Identical Venue P9.8 native PCM remains the mandatory baseline.
- P9.8 `4a9e5b2c455818c7a0dc8507ec2b7863be215033` is the independent scene-voicing baseline, *not* overwritten. P9.9 is stacked in a separate opt-in draft branch/PR.
- P9.9 adds no audio device, callback, IR convolution, PCM graph, 96k resampling, WebAudio sidecar, neural model or new .smix state schema. It is an extra compile-time stage in the **existing single native graph** for typed scene IDs 1 Signature, 2 Studio, 3 Cinema, only in active world-speaker mode9 with ROOM ON and spatial MIX>0.
- VENUE / Custom scene ID 0 (including old restored v11 state), ROOM OFF, and MIX=0 **must remain exact P9.8 bytes** at 44.1/48k and front/rear; explicitly compare native PCM hash. No change to spatial placement, scene preset values, 10m Venue, curves, P98 class or KEMAR HRIR.
- Private `ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH=OFF` default requires opt-in P9.8. Native bridge remains *v12*, with truthful additive `p99MusicalVocalDepth` capability (never infer from GUI). The ordinary public build remains unchanged.

## Engineering rationale from global audio research

Full stereo recordings do not provide separated singer and backing stems. A center-frequency boost alone elevates guitars/piano/snare too; a center-dominant *activity estimate* is a conservative guide, not actual voice identity. This project must never claim vocal separation or independent vocalist positioning. Relevant research:

- Yuval Ronen, AES 139 Paper 9445 (2015), [Vocal Clarity in the Mix](https://aes.org/publications/elibrary/elibrary-page/?id=18001): professional mixing techniques can improve *perceived* intelligibility; perceptual testing is needed.
- Wakefield & Dewey, AES 138 Paper 9341 (2015), [Frequency Masking in Multitrack Music](https://aes.org/publications/elibrary/elibrary-page/?id=17765): spatial separation/panning is a useful alternative to large mirrored EQ boosts.
- Kirsten Hermes, AES 146 Paper 10174 (2019), [Spectral Clarity of Vocals](https://aes.org/publications/elibrary/elibrary-page/?id=20307): timbral clarity depends on program; simply adding high-frequency content is not universally safe.
- Hafezi & Reiss, JAES 63(5) (2015), [Autonomous Multitrack EQ Masking Reduction](https://aes.org/publications/elibrary/elibrary-page/?id=17637): masking relief can improve intelligibility when instrument stems are available—**not equivalent** to doing stem-specific EQ on one stereo master.
- Wichern et al., AES 141 Paper 9646 (2016), [Masking and Loudness Loss](https://aes.org/publications/elibrary/elibrary-page/?id=18450): not all spectral overlap is problematic, so avoid indiscriminate maximal unmasking.

## Implemented single-graph acoustic strategy

1. **Direct center articulation, full-bodied**: source before the existing measured loudspeaker KEMAR FIR. Two broad first-order complementary bands (210–880Hz body, 760–3350Hz articulation), at bounded profile weights. The gate follows a smoothed ratio of in-band MID to SIDE and a minimum audible level; its 21ms/122ms envelope + 32ms/187ms gain attention avoid per-syllable switches. A 55ms scene change ramp prevents step clicks. Sig > Cinema > Studio intensity.
2. **Lateral instrument atmosphere without fake Haas**: only the *already physically modeled first-image stereo reflection* receives a tiny complementary 680–3900Hz SIDE emphasis (maximum 0.08 linear); the MID and source phase are unchanged. No added random delay, decorrelation, stereo phase rotation, width collapse or rear center duplication.
3. **Vocal vs backdrop depth**: when center dominance is strong, slightly relieve the 720–3400Hz MID of the *existing late room return* (maximum 0.095 linear), never its SIDE or bass. Gives the real direct center more contrast to the diffuse surroundings. The floor/ceiling/side reflection network and room tails otherwise remain authoritative.
4. **No 4–8k treble/sibilance shelf**; no high-pass stripping at 250Hz, hard expander, multiband limiter, or vocal-only fake stem. Preserve bass impact and 0.98 linked peak guard, and reject non-finite PCM in existing core.
5. **No significant CPU overhead**: fixed scalar one-pole states, constant 4 profile vectors, no heap allocation/block buffer/FFT/convolution beyond the existing P90 path, no extra sample latency. Exact CPU saving is not proven by source tests; real Windows performance/ASIO headphone audition remains operator FAT.

## Validation matrix and acceptance limits

- Standalone **44.1/48k**: male-range 260Hz body must not thin; 1800Hz increases vocal-band center audibility; 7600Hz bounded (not harsh); real early-IMAGE side expands slightly; late center band reduces; late SIDE and anti-phase direct remain unchanged; near silence avoids pump; on-the-fly Venue and ROOM OFF exact.
- Full wet actual KEMAR+room+P95-P98 graph: P9.8 vs P9.9 **24 cases** (44.1/48k × front/rear × Venue/Signature/Studio/Cinema/ROOM OFF/MIX0), exact hashes for Venue/ROOM OFF/MIX0; no original bass deletion or 7500Hz excessive boost; low max linked peak guard.
- Native JUCE v12 ACK and old v11 migration regressions; compile/test on Windows x64 and Chromium front-end capability contract; package full GUI portable executable with SHA provenance and this FAT guide. No automatic merge/release without native Windows CI and a level-matched listener test.

## Honest remaining limitations and hearing FAT

Mid/side gating can enhance a centered guitar/snare/piano, not only a singer. No algorithm can distinguish overlapping mono-center sources with certainty without separate stems or model-based extraction. The confidence detector **must remain conservative** and can occasionally act on instrument-only centered content.

Use level-matched male/female vocal recordings, dense rock drums/guitars, piano/voice, sibilant vocal, cymbal-heavy instrumental, mono full mix, deliberate 180° anti-phase, and front/rear stage motion. Listen for chest body, articulatory consonants, non-nasal tone, no new sharp 3–8k peak, no center ghost, no pumping, no collapsed room or bass. Quantitative PCM/spectral tests do not prove the intended **subjective** intimacy or audience goosebumps. In all doubt restore the P9.8 reference; the Venue P9.7 checkpoint is non-negotiable.
