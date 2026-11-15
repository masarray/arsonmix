# SonKu P9.0 — Native Stereo External Speaker Candidate (DRAFT, 2026-11-15)

## Scope and version control

New opt-in `MusicSpatialP90 = 9`, on the exact draft P8.9 branch lineage. All SonKu P8.2–P8.9 modes and current production mixer GUI/routing remain separate. No merge, public release, or subjective claim is authorized. Current `main` has the separate #713 audio-device continuity fixes: P9 source branch is deliberately **not** a substitute for #713 and must not be blindly merged to `main`.

## Why P8.9 is not enough

P8.4-P8.9 use `Hc + 0.12 HP750(Hspeaker-Hc)` and independently add wide stereo SIDE (HP220) directly to headphone L/R. At 6.7m the SIDE retains approximately 1x original near-field energy while directional direct speaker is reduced by ~0.42x. The resulting treble remains ear-locked even when virtual speakers rotate, while early reflection gains are comparatively modest. Merely raising 0.12 would reproduce untested P8.3 music damage.

## New native DSP: coherent directional transfer

P9 bypasses **only in new mode 9** the old P8.4 12%-contrast kernel blending and after-FIR headphone SIDE rescue. It uses the same single 2×2 native FIR mixer, with full MIT KEMAR measured directional kernels (`H_L,H_R`) and measured midpoint kernel `H_c`. Each kernel is a real impulse response encoding complex frequency response and ITD. Let `c` be a gently smoothed 0..1 positive interchannel-coherence evidence, initialized to 1. At every existing 16-frame FIR retarget:

```
A = (H_L + H_R)/2
H'_L = H_L + c*(H_c - A)
H'_R = H_R + c*(H_c - A)
Y = H'_L * L + H'_R * R
  = [H_L + H_R + 2c(H_c-A)]*M + [H_L-H_R]*S
where M=(L+R)/2, S=(L-R)/2
```

The stereo SIDE transfer **does not depend on `c`**. At `c=1`, equal L/R produces exactly the measured center HRIR without a dual-speaker correlated-MID comb. At `c=0`, independent channel sources see their full measured speaker responses. No second renderer, extra audio callback, minimum-phase replacement, spectral high bypass, or anti-phase correlation gate. `c` uses 80ms input correlation evidence, a 120ms response, and one square root per 16-frame retarget; all state fixed-size and allocated before audio. All kernel coefficients continue using the original per-sample slew.

Important physical degenerate case: stereo **0° angle** places two identical real speakers at the same point. Opposite-polarity inputs then cancel at the source; no physically honest linear two-source binaural renderer can simultaneously make their contributions fully independent and render them as exactly co-located. Use 60–180° for nonzero anti-phase localization; do not falsely promise anti-phase retention at 0°. The GUI explicitly says so.

## Existing model retained, limitations and follow-up

P9 currently retains P8.9 distance-to-direct/LF gain, world-head yaw, measured spectral elevation trim, six-wall directional early field, and late FDN. It uses the existing P8.9 long-distance early/diffuse ratio. No fresh spectral HRIR time alignment, phase-aware angular interpolation, personalized HRTF, headphone calibration, BRIR room measurement, or optimized room-distance model has been proven yet. It is an **architectural first candidate**, not the completed 100%-immersive target.

## Objective qualification

The new native PCM test uses 44.1/48k; 64/128/256 buffer-invariant output; source L-only, R-only, correlated MID, anti-phase and complex stereo; azimuth, head yaw, 60/104/180° angles; 1.6/6.7/10m scenes; nonzero finite output, no >0.98 peak, changing PCM vs P8.9, HRTF kernel algebra. Existing P8.4–P8.9 regression suites remain mandatory. Native JUCE v11 test covers GUI/bridge-compatible schema, applied ACK, output PCM and unchanged v1..v10 state restores. CPU profiling, HRIR directional ILD/ITD curves, DRR/IACC, and genuine physical ASIO are **not yet qualified**.

## Headphone FAT

1. Run an isolated Windows x64 portable candidate with no other spatial FX enabled, stereo headphones, and equal RMS loudness.
2. Compare P8.4, P8.7, P8.8, P8.9, P9 at 1.6/3/6.7m and 60/104/180° angle. Use the same full-range stereo reference and anti-phase/detail torture material.
3. Test stationary and slow yaw -90/0/+90 and elevation 0/±10° with ear-height spectral trim. Confirm each speaker and all treble actually rotate in WORLD.
4. Rate vocal center naturalness, treble source localization outside head, bass body, width, ceiling/floor confusion, silence, transient smear, and long-distance perception. Record precise settings and the source commit; do not equate passing CTest with perceptual dearVR parity.
5. Verify session close/reopen and device dis/reconnect separately against the #713 integrated `main` source before merging any acoustic patch.

## Publicly documented research reference

- Best et al. (2020), *Sound Externalization: A Review of Recent Research*, https://pmc.ncbi.nlm.nih.gov/articles/PMC7488874/
- dearVR PRO 2 user manual (2024), https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf — reflections, room and distance externalization
- Sennheiser AMBEO CLARITY guide, https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2 — perceptual localization/tonal tradeoff

This method is original engineering of public binaural principles, **not** a claim about proprietary dearVR internal code.
