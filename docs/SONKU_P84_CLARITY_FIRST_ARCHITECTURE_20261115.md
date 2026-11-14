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

Formula: output = P8.2 + 0.12 * highpass_750Hz(P8.3Physical - P8.2)

- P8.2 is the primary music signal; influence zero is **bit exact**.
- Direct P8.3 world-fixed HRTF directional contrast is a small HF research term, with no fabricated LF matrix, makeup gain, correlation detector or extra room tail.
- A 1-pole differentiator crossover is bounded, allocation-free, lock-free, and has no lookahead.
- 0.12 is a **research candidate**, not perceptually optimized.
- Important physical limitation: the P8.2 anchor remains head-locked while the difference follows head-relative orientation. This is NOT a fully consistent measured BRIR or a demonstrated externalized room. Its purpose is rejecting or supporting the feasibility of a music-preserving localizer.
- Two native SonKuSpatialCore instances are used ONLY in an OFFLINE analytical test. The full dual-core processing overhead is forbidden in production. If validated, fuse to a single native DSP graph before JUCE integration.

Test: tests/sonku_p84_clarity_candidate_test.cpp — actual C++ SonKu DSP, not WebAudio or mocked responses. Tested 44.1/48k, seven program types including impulse, (mono, anti-phase, hard L/R, partially correlated, transient), ROOM ON and OFF, ten frequencies (83–9301Hz), PCM 64/128 parity, full-band output RMS <=1.5dB difference to P8.2, each applicable tone <=2.5dB, finite/no clipping, byte-exact zero-influence P8.2 bypass and head-yaw differential.

This is only a necessary *source qualification*. Passing tone and RMS checks does not demonstrate natural timbre on real music, front/back separation, externalization, IACC, headphone-specific frequency response, or good CPU p99.

## Original next gates (status updated in single-core milestone below)

1. Run wideband multi-content FFT/erb spectral distance, ITD, ILD, transient and phase checks, room DRR and measured direct/early/late impulse responses, with P8.2 and the rejected P8.3 as controls. Avoid tuning only to hand-picked tones.
2. Use a properly licensed SOFA/AES69 measured near-field BRIR dataset and, later, personalized headphone compensation and live head tracking. Ensure all scene signals respond consistently to world-fixed yaw; distinguish stereo speaker monitoring from 3D audio object spatialization.
3. Replace double offline renderer with **ONE** JUCE native PCM callback and coherent fused measured transfer if acoustic fidelity gate holds. Keep SPSC/ACK, state rollback, .smix, native GUI, no M4 alternate routing; verify CPU at 44.1/48 kHz, buffer 64/128/256, 1/2/4 inserts.
4. **Before** Windows portable/merge/public release, physical level-matched headphone tests against P8.2 and dearVR with multiple songs, blind directions including front/back, externalization and overhead false-positive ratings, ASIO XRUN evidence.

Do not merge/release PR #701 or this experimental stack without owner-approved headphone FAT. The approved P8.2 listening baseline is preserved.

## First native response sweep (same-source 44.1/48 kHz)

The initial 18% directional-contrast strength passed all six broadband
RMS program checks (typical ~-0.45dB relative to P8.2), but failed
timbre spectral limits, with a worst probe at 5387Hz around **-3.21dB**
under ROOM ON at both rates, plus 2891Hz coloration and antiphase
9301Hz dips. The numerical limit is unchanged: +/-2.5dB per measured
frequency and +/-1.5dB broadband per program. The next 12% candidate
is derived from the measured failure, not gain makeup. Whether such a
limited difference produces persuasive headphone localization is
**UNKNOWN**; a spectral PASS must never be called externalization.

## Second evidence milestone: independent broadband room impulse qualification

P8.4 research source gate at exact commit
`f531324cb3f9a93f2f8ed066f0f9cd1914af9883`:
https://github.com/masarray/arsonmix/actions/runs/43640451156

The source now includes an impulse-program FIR/room test and an
integrated one-third-octave spectral-response scan from 90Hz to
approximately 12kHz (11 bands per rate/room, each integrating five
frequency probes). This is independent from the 10 musical tone
frequencies and checks ROOM OFF and ROOM ON separately.

Native Linux source results: **28 total rate/room/program fixtures**
(44.1/48k, room OFF/ON, seven programs including impulse), **88
third-octave impulse measurements** and **280 fixed-tone measurements**;
worst impulse-band relative deviation **2.182dB at 3.63kHz**
(48k ROOM OFF), absolute worst program broadband level deviation
**1.267dB** versus P8.2 (impulse fixture), and yaw remains measurable.
Both source quality gates passed without gain normalization or relaxed
thresholds. These tests are not calibrated to a listener's headphones
and still do not establish front/rear realism, distance or commercial
dearVR parity. Next stage must replace dual-core evaluation with a
single physical graph, perform listening and measured BRIR/ear
calibration; do not offer a Windows test until then.

## Stage 2: single-PCM-core fusion — completed SOURCE research, not JUCE FAT

Implemented new, independently opt-in
`SONKU_P84_SINGLE_CORE_FUSION_RESEARCH=1`:
- `engine/builtin_fx/sonku/SonKuP84FusedMusicHrir.h`:
  `H_fused(source, ear) = H_P82_center + 0.12 * HP750(H_measured_speaker - H_P82_center)`
  in **140-tap finite impulse coefficient space**, recomputed at existing
  16-sample target updates. This truncates the IIR high-pass tail at 140
  taps; tests independently measure the induced spectral effect. Exactly
  **one original SonKuSpatialCore** processes the PCM, using four direct
  FIRs; the second P83/independent PCM engine is gone from the hot path.
- Preserves P8.2 transparent stereo bass, headphone SIDE, music-safe
  center anchor and room gain law. No LF480 invented matrix, no AGC,
  no copied proprietary Clarity, no extra dry playback route, no
  allocations/locks/FFT in the sample callback.
- Research setter `setResearchFusedMusicClarity()` is allowed only
  while quiescent, explicitly enabled, and on a real six-wall KEMAR
  native profile; no public GUI, v4 schema or session changes.
- Room EARLY geometry gets the same simulated head yaw as the fused
  directional HRTF. The P8.2 center music anchor remains head-locked;
  this is still NOT a genuinely head-tracked whole-scene BRIR.
- Current code depends on historical *rejected* P8.3 source headers.
  **PR #704 is stacked on rejected PR #701 and must NOT be merged via
  that chain**. If ear tests approve the new approach, extract the
  fused transfer and necessary geometry alone into an approved
  P8.2-based branch; discard the rejected physical/LF480 renderer and
  legacy failed host modes. Avoid premature code migration.

### Reproducible source CI — exact current candidate

[Source run 43644299478](https://github.com/masarray/arsonmix/actions/runs/43644299478):
28 actual native music-like/impulse render cases at 44.1/48k, ROOM
ON/OFF, 280 fixed-frequency and 88 one-third-octave IR comparisons,
worst RMS deviation **1.264 dB**, max frequency deviation **2.204 dB**
relative to P8.2, within strict gates. 64/128 block PCM parity and
simulated yaw changes with both ROOM ON/OFF pass.
Cross-compilation with P8.4 macro OFF versus ON when fusion disabled
produces **bit-identical** P8.2 raw PCM at both sample rates, ROOM ON
and OFF, mono and anti-phase.

48k native mono impulsive direction diagnostic: ±90 degree ILD
**-16.602/+16.602 dB**, signed ITD-correlation lag **±35 samples**,
IACC magnitude **0.39053** at ±90, front/rear impulse difference
nonzero, and room/direct motion both respond to static simulated yaw.
Measured energy ratio direct window vs late tail window
**19.257 dB**, NOT a standardized calibrated DRR measurement and
NOT proof of real-room externalization.

### CPU evidence and real-music A/B tool

[Full source run 43644595928](https://github.com/masarray/arsonmix/actions/runs/43644595928)
PASS: 44.1/48k, buffer 64/128/256 and 1/2/4 native instances,
18 cases (baseline vs fused). In one hosted Linux run for 48k, buffer
64, **4 inserts**, P8.2 p50 **744.20 µs**, P8.4 p50 **785.80 µs**,
P8.4 p99 **833.16 µs**, no deadline misses in sampled blocks. Median
overhead was typically ~5%, not a measured Windows ASIO CPU/XRUN
guarantee.

New `tools/sonku_p84_offline_audition.cpp` accepts a supplied
stereo WAV (PCM16/24/32 or float32, 44.1/48k, including standard
WAVEFORMATEXTENSIBLE), preserves its original RMS and identical
render settings, and outputs four float32 WAVs:
`P82_Golden.wav`, `P84_FusedYaw0.wav`,
`P84_FusedYawPlus60.wav`, `P84_FusedYawMinus60.wav`,
plus CSV relative RMS/peak. CI smoke validates both ROOM ON and OFF
and each file's IEEE-float RIFF/fact payload. No loudness makeup,
master processing or proprietary recordings. Real-song headphone
review remains indispensable; CI uses only synthetic probes.

### Remaining qualification gates

* Multi-song real-music PCM: loudness, timbre, drums, attack, phase,
  interaural coherence, tonal fatigue, reliable audition A/B, blinded
  ear-level calibration and signal provenance.
* True natural externalization: correctly matched listener HRTF,
  headphone EQ, speaker distance/source directivity and
  measured/licensed BRIR; plausible reflection levels and 3D motion.
  Research on frontal BRIR externalization and early-reflection level
  provides hypotheses, **not universal gain presets**:
  https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/
* Low-latency head tracking with world-fixed direct + early + late
  cues, SPSC/ACK, smoothed control and .smix versioning only after
  source and physical audio acceptance.
* Single native JUCE host, ASIO p50/p95/p99, actual headphone
  front/back/elevation confusion matrix and direct dearVR A/B.
  A source-ci PASS alone does NOT authorize Windows portable or public
  release. PR #701 remains rejected and P8.2 golden untouched.
