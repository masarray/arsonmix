# SonKu P4 experiment — contiguous 140-tap reflection HRIR without extra ring
2026-11-14 · draft PR #674 · offline only; performance unverified pending CI

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
