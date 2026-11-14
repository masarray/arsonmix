# SonKu P8.4 — Musical-fidelity-first externalization research

Date: 15 November 2026 WIB. **Research only, NOT an accepted headphone candidate.** The owner rejected the actual P8.3 native mixer as quiet, boxed-in and hollow; P8.2 is the golden audible baseline. PR #701 remains rejected and unmerged.

## Evidence and independent external research

- Sennheiser says the Clarity parameter in dearVR PRO 2 trades HRTF spectral coloration against localization/externalization, and the best compromise varies between voices and instruments: https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2
- AES paper on Perceptually-Optimized Headphone Virtualizer: maximize sound-source externalization under a natural-timbre constraint, with subjective A/B validation: https://aes.org/publications/elibrary/elibrary-page/?id=18286
- Sound Externalization review: non-individual HRTFs, inaccurate headphone equalization, head motion and room acoustics can impair externalization: https://pmc.ncbi.nlm.nih.gov/articles/PMC7488874/
- SOFA/AES69 is a measurement interchange format for HRTFs and room IRs, not an unconditional license to reuse data: https://www.sofacoustics.org/
- The SADIE virtual loudspeaker dataset includes measured diffuse-field compensated HRTFs, ITD data and specific loudspeaker array measurements: https://pure.york.ac.uk/portal/en/datasets/sadie-virtual-loudspeaker-database/

**No proprietary algorithm is copied.** The P8.4 code does not implement Sennheiser's patented Clarity.

## Actual P8.3 causes

The rejected 'positionedMode' explicitly bypassed P8.2's transparent stereo bass, music-safe centered HRTF and SIDE preservation. It also excluded the P8.2 room-presence compensation. Independent +/-30° HRIR stereo transfer and an invented 480Hz bass matrix sounded weak. Four isolated frequency probes were NOT an adequate music-quality test. The initial field listening result must override previous numeric PASS claims.

## Stage 1 completed as source code: offline musical anchor experiment

New isolated experimental class: engine/builtin_fx/sonku/SonKuP84ClarityAnchor.h

Formula: output = P8.2 + 0.18 * highpass_750Hz(P8.3Physical - P8.2)

- P8.2 is the primary music signal; influence zero is **bit exact**.
- Direct P8.3 world-fixed HRTF directional contrast is a small HF research term, with no fabricated LF matrix, makeup gain, correlation detector or extra room tail.
- A 1-pole differentiator crossover is bounded, allocation-free, lock-free, and has no lookahead.
- 0.18 is a **research candidate**, not perceptually optimized.
- Important physical limitation: the P8.2 anchor remains head-locked while the difference follows head-relative orientation. This is NOT a fully consistent measured BRIR or a demonstrated externalized room. Its purpose is rejecting or supporting the feasibility of a music-preserving localizer.
- Two native SonKuSpatialCore instances are used ONLY in an OFFLINE analytical test. The full dual-core processing overhead is forbidden in production. If validated, fuse to a single native DSP graph before JUCE integration.

Test: tests/sonku_p84_clarity_candidate_test.cpp — actual C++ SonKu DSP, not WebAudio or mocked responses. Tested 44.1/48k, six program types (mono, anti-phase, hard L/R, partially correlated, transient), ROOM ON and OFF, ten frequencies (83–9301Hz), PCM 64/128 parity, full-band output RMS <=1.5dB difference to P8.2, each applicable tone <=2.5dB, finite/no clipping, byte-exact zero-influence P8.2 bypass and head-yaw differential.

This is only a necessary *source qualification*. Passing tone and RMS checks does not demonstrate natural timbre on real music, front/back separation, externalization, IACC, headphone-specific frequency response, or good CPU p99.

## Next architectural gates (NOT completed)

1. Run wideband multi-content FFT/erb spectral distance, ITD, ILD, transient and phase checks, room DRR and measured direct/early/late impulse responses, with P8.2 and the rejected P8.3 as controls. Avoid tuning only to hand-picked tones.
2. Use a properly licensed SOFA/AES69 measured near-field BRIR dataset and, later, personalized headphone compensation and live head tracking. Ensure all scene signals respond consistently to world-fixed yaw; distinguish stereo speaker monitoring from 3D audio object spatialization.
3. Replace double offline renderer with **ONE** JUCE native PCM callback and coherent fused measured transfer if acoustic fidelity gate holds. Keep SPSC/ACK, state rollback, .smix, native GUI, no M4 alternate routing; verify CPU at 44.1/48 kHz, buffer 64/128/256, 1/2/4 inserts.
4. **Before** Windows portable/merge/public release, physical level-matched headphone tests against P8.2 and dearVR with multiple songs, blind directions including front/back, externalization and overhead false-positive ratings, ASIO XRUN evidence.

Do not merge/release PR #701 or this experimental stack without owner-approved headphone FAT. The approved P8.2 listening baseline is preserved.
