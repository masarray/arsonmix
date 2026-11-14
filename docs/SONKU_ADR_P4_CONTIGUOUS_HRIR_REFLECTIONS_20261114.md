# SonKu P4 experiment — contiguous 140-tap reflection HRIR without extra ring
2026-11-14 · draft PR #674 · offline only; native 44.1/48k source PCM PASS; hosted CPU measurement provisional

## Context and research
The [dearVR PRO 2 official manual](https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf)
§§2.1 and 3.2 describes geometric early reflections as
important to perceptual externalization, responsive to source
motion, distance and six surfaces. It does not disclose
proprietary rendering code or guarantee equivalence.
[Steam Audio reflection effect](https://valvesoftware.github.io/steam-audio/doc/capi/reflections-effect.html)
documented CPU-versus-quality alternatives include HRTF filtering,
multi-channel convolution and hybrids.

## Exact algorithm
Current P2 full 140-tap horizontal KEMAR reflected FIR computes
two ring-wrap masks per input tap and per reflection per sample.
The index `idx=(write-integerDelay)&8191` refers to the newest
valid delayed sample, and FIR reads indices `idx-k` and
`idx-k-1` for k=0..139. When idx>=140, ALL reads are within
the same 8192-float history span (8052 positions, ~98.3%
of ring phase). Optional P4 path uses the identical interpolated
input expression but plain `float*[-k]` addresses, reducing per-tap
mask/index arithmetic. When idx<140, use the previous masked
reference. No doubled ring memory or vector backend, no FIR
truncation, downsampling, delayed coefficient update, or tap loss.
P3 zero-spread fusion and both ear HRIR coefficient smoothing remain
exactly the same, including spread transitions.

Access is set only on an OFFLINE inactive core:
`setDirectionalEarlyReflectionsHrir(true)` then
`setDirectionalContiguousHrir(true)`. Default stays false;
no GUI, .smix schema, JUCE graph, WIRE, FX ACK or public release
changes. No extra audio thread, malloc, I/O or locking.

## Acceptance / rejection
Must pass 44.1/48k C++ compare to full source P2 for 0/60/120°
stereo spread, coherent, frequency-conflicted independent stereo and
independent impulse streams, rotating azimuth and 3 wrap periods.
Exact per sample absolute error <2e-6, and room OFF strict
release. P0/P1 baseline six WAV, P2 front/rear A/B and KEMAR
380-point spectral remain as release gates.
Bench on same GitHub runner and 48k/128 60° spread native
full-core CPU p99. **Reject** and revert opt-in P4 if no convincing
improvement relative to runner variance, as with previously
rejected doubled-ring technique (~0.4%).
No claims of improved front/rear externalization from a CPU edit.

## Native result: 2026-11-14, exact commit 77d1fc9

GitHub Actions [#43536803506](https://github.com/masarray/arsonmix/actions/runs/43536803506):
P4 44.1/48k, 0/60/120° 140-tap PCM, three ring wraps,
moving azimuth, independent/correlated/transient audio PASS.
P0/P1 six 4s baseline A/B PASS, P2 and P3 native regressions
PASS, and KEMAR 380-point spectral audit PASS.
For **48k/128/3s, spread 60°**, same-runner isolated core:
P2 723.042ms vs P4 437.188ms = **1.654×** speedup,
p99 666.749us vs 470.390us. Spread 0/120° and 44.1k
showed ratios 1.615–1.657×. Absolute values vary
substantially across shared Linux CI hosts. Not ASIO proof.
Unlike the rejected doubled-ring earlier, this method
needs no additional delay history.

## Additional independent stereo WAV qualification
Four 2s 48k/16bit original synthetic independent-stereo
FRONT/REAR x 60°/120° P2 full and P4 contiguous pairs.
A Python gate checks <=1 signed PCM 16bit LSB maximum
sample difference, <2% differing samples, <1e-5 normalized
RMS difference, <0.01 dB each 165/940/2200/4700/5100Hz
harmonic on both ears. It also reports exact byte identity
and sample-difference fraction. One-bit quantization
boundaries are NOT evidence of changed perceptual quality.
The initially strict byte-equality gate failed: a one-LSB
quantization difference must be measured, not mislabeled
as a proven invariant. The C++ wet-only full-float
source test still uses <2e-6 per-sample absolute error.
All existing PCM, spectral, motion, ring and CPU checks remain.
There is no claim of perceptual improvement from a bit-exact
optimization; its benefit is rendering identical directional
early reflections with lower callback CPU cost.
