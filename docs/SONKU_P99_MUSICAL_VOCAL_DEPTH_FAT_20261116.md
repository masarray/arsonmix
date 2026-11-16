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
3. **Physically coupled room body (P9.9 room correction)**: the original approach enhanced direct history_ while the real `room_.process` and `reverb_.process` received original x0/x1. That inconsistency made the enriched singer sound *too dry/less physically present*. Feed an activity-gated **165–670 Hz coherent MID band** into the pre-existing true directional first-images and diffuse reverb, using separate Signature ER 0.48 / late 0.22, Cinema 0.34 / 0.15, Studio 0.22 / 0.095 scalar send weights. No extra delay/feedback/second room/binaural crossfeed. The room remains frequency-dependent through its own native physical geometry and acoustics. This is a musically bounded warm band, NOT a claimed isolated vocal stem or a synthetic high-Q resonant mode. The existing L/R SIDE is unchanged.
4. **Vocal vs backdrop depth**: preserve the 200–650 Hz vocal room body; slightly relieve only 1350–3800Hz MID of the *existing late room return* (maximum 0.095 linear), never its SIDE or bass. This retains articulatory definition while low-mid reflections/tail supply room presence. The floor/ceiling/side reflection network and room tails otherwise remain authoritative.
4. **No 4–8k treble/sibilance shelf**; no high-pass stripping at 250Hz, hard expander, multiband limiter, or vocal-only fake stem. Preserve bass impact and 0.98 linked peak guard, and reject non-finite PCM in existing core.
6. **Bounded real-time overhead**: fixed scalar one-pole states, six-coefficient scene profiles, no heap allocation/block buffer/FFT/convolution beyond the existing P90 path, no additional samples of latency. Filtering into the existing room adds two biquad warm-band filter states and feed mixing. Actual CPU margin and ASIO headphone audition remain operator FAT.

## Validation matrix and acceptance limits

- Standalone **44.1/48k**: male-range 260Hz body must not thin; 1800Hz increases vocal-band center audibility; 7600Hz bounded (not harsh); real early-IMAGE side expands slightly; late center band reduces; late SIDE and anti-phase direct remain unchanged; near silence avoids pump; on-the-fly Venue and ROOM OFF exact.
- Full wet actual KEMAR+room+P95-P98 graph: P9.8 vs P9.9 **24 cases** at 86/2050/7500 Hz and a separate **24-case, complete six-wall room 320/2050/7500 Hz probe** (44.1/48k × front/rear × Venue/Signature/Studio/Cinema/ROOM OFF/MIX0). Demand warm 320Hz response in scene1/2/3, not merely new dry EQ; exact hashes for Venue/ROOM OFF/MIX0 in both sets; no original bass deletion or 7500Hz excessive boost; linked peak guard.
- Native JUCE v12 ACK and old v11 migration regressions; compile/test on Windows x64 and Chromium front-end capability contract; package full GUI portable executable with SHA provenance and this FAT guide. No automatic merge/release without native Windows CI and a level-matched listener test.

## Honest remaining limitations and hearing FAT

Mid/side gating can enhance a centered guitar/snare/piano, not only a singer. No algorithm can distinguish overlapping mono-center sources with certainty without separate stems or model-based extraction. The confidence detector **must remain conservative** and can occasionally act on instrument-only centered content.

Use level-matched male/female vocal recordings, dense rock drums/guitars, piano/voice, sibilant vocal, cymbal-heavy instrumental, mono full mix, deliberate 180° anti-phase, and front/rear stage motion. Listen for chest body, articulatory consonants, non-nasal tone, no new sharp 3–8k peak, no center ghost, no pumping, no collapsed room or bass. Quantitative PCM/spectral tests do not prove the intended **subjective** intimacy or audience goosebumps. In all doubt restore the P9.8 reference; the Venue P9.7 checkpoint is non-negotiable.


## Correction rationale — direct sound and acoustic-body consistency

The operator correctly identified that P9.9 initially strengthened the CENTER of the direct KEMAR virtual speaker input while both ROOM processors still observed the unchanged stereo master. Furthermore, a 720–3400 Hz late-mid reduction made the vocal seem even more dry/detached. It did **not** mean there was zero room sound; the issue was inconsistent excitation/voicing between direct and reflected paths.

This revision deliberately sends a two-biquad, activity-controlled, identical L/R low-mid contribution to the original room engines BEFORE their existing wall/late response. It does not reroute all P9.7/P9.8 tonal boost (which would over-excite snare, cymbals and LF). Only P9.9 scene IDs 1–3, room ON, wet MIX>0 get this feed. A source with no center signal, or a near-silent gate, returns exact original stereo. The user-approved Venue scene ID0 / ROOM OFF / MIX0 remain cold-start native PCM unchanged.

See background on direct/reverberant balance and C50/C80 clarity: https://pmc.ncbi.nlm.nih.gov/articles/PMC6375510/ and https://aes.org/publications/elibrary/elibrary-page/?id=14920 . A little early reflection often increases fullness and can improve articulation; excessive late energy can mask words. This is NOT a proof of vocalist identification in a stereo mix, nor a promise of subjective superiority before listening.


## DSP gate rejected — first room feed LF cancellation

The first warm-band attempt (difference of 165Hz and 670Hz second-order low-passes) made an apparent +47% direct ER320 input, but **failed full-graph P9.8/P9.9 86Hz regression at 0.929×**. This is an important counterexample: extra low-mid energy may cancel sub/bass because of filter phase and existing six-wall paths. The test was **not relaxed**. Replaced this imperfect difference response with a pair of stable, centered 330Hz Q1.4 biquads whose lower-octave magnitude rolls off strongly, and reduced sends so LF remains within the existing approved bound. The verification harness still rejects any decrease below 96% at 86Hz and any peak above the previously approved bounds.

No claim that boosted ER input by itself proves a better final acoustic output. Always check complete KEMAR+room+limiter PCM and listener A/B.


## 2026-11-16 operator screenshot → P9.9 Default Orbit Stage

The operator provided a screenshot of **CUSTOM · MANUAL** with ORBIT (not STATIC), CLOCKWISE, 12.0 s revolution, 127° speaker angle, 100% spatial mix, +5.1 dB display gain, Large 28.8×28.8m, 2.7m source distance, elevation 0°, HRTF ear-height align 0°, HEAD YAW 0°, Reflections 100%, Reverb 77%. Input provided a visual speaker placement mid-orbit; this does NOT reveal a persisted initial azimuth. Start azimuth is explicitly set to **FRONT 0°** for a deterministic new orbit, and gain **1.8 linear** renders approximately +5.1dB as in the screenshot. The screenshot does not expose the native `sceneVoicing` byte: choose **typed Signature ID1** instead of neutral ID0, otherwise the P9.9 center-body/room coupling would never activate.

- New `ORBIT STAGE · DEFAULT` (GUI id `orbitStage`) is an **additional opt-in P9.9 preset**, not a rewrite of existing `music`, `studio`, `wide`, `deep` (approved Venue), or `orbit`.
- The authoritative C++ `DefaultOrbitStage::settings()` is installed by `SonKuSpatialProcessor` only for brand-new opt-in P9.9 inserts. Native v12 state and ACK are unchanged; saved `.smix` / v11-v12 restore and operator Custom states are never silently changed. UI label matches the native readback and is unavailable to P9.8-only binaries.
- Previously, `sameSettings()` ignored `sceneVoicing` (and HRTF elevation trim): a pure scene-ID change could be silently treated as a no-op. Now both typed parameters participate in native command deduplication. This is required for presets to change the **real DSP**, not merely their visible names.
- This **user-guided stronger Signature** steps the limited P9.9 weights from `{.095,.165,.080,.095,.48,.22}` to `{.112,.200,.105,.108,.55,.25}` (body, focus, directional early, late-mid relief, 330Hz early-room feed, late-room feed). These are bounded *mixing coefficients*, not extra dB, no independent stem extractor. Live Studio and Cinema are unchanged. Venue/neutral remains exactly zero-weight.
- Acceptance: independently test exact new factory numbers, native JUCE NEW insert save/reopen, no default migration for old sessions, 1s native orbit = +30° at 44.1/48k, scene-only queued edits actually transmit, safe peaks, early 330Hz body, 1.8k articulation, 7.6k sibilance cap; full original P9.8/P9.9 static 24-case Venue PCM bit-exact tests must still pass. Real level-matched headphone listening remains required; do not release because numeric tests alone pass.

Screenshot application footer reads `NATIVE IMMERSIVE · SCENE VOICING · VENUE LOCKED`, not `MUSICAL VOCAL DEPTH · REAL STAGE`; it appears to be a P9.8-capable binary rather than P9.9. Do not represent any P9.9 improvements as already auditioned on that screenshot without matching executable build SHA and native capability.
