# SonKu P7.0 — six-surface measured-elevation KEMAR reflection pilot

14 November 2026 · research branch stacked on #678 → #674 · **NOT public/realtime-approved**.

## Scope and source

The existing P6 native direct HRTF, four-wall image sources, P0/P1 stereo
integrity, P5 fresh-history guard, .smix v1/v2 and UI are NOT changed in
the shipping default. The new internal `SixSurfaceKemarResearch` profile
can only be prepared in a build compiled explicitly with
`SONKU_P7_MEASURED_ELEVATION_RESEARCH=1`. Public JUCE release binaries
do not define this flag; not a GUI/session parameter.

A fully measured MIT KEMAR compact normal-pinna bank is pinned in
`SonKuMeasuredHrir3D.h` for 44.1k (128 zero-padded to 140 taps) and
48k (140 taps): 14 elevations **−40° through +90°**, with original
azimuths for EACH elevation (not fake uniform 5°). 368 original
measurement pairs per sample rate, excluding duplication of the existing
0° bank. Negative azimuths swap ears. 0° retains the original bank
bit-for-bit; upper and lower elevation interpolate actual HRIR taps.
Source provenance: MIT HRTF C Library, original KEMAR Gardner/Martin
measurements compiled by Aristotel Digenis under MIT, source blobs
`5e5bcac17b0d59f1b8e7fee04b380170f7d521d2` (44.1k),
`39931add922dad4687e572059bc157b124926944` (48k),
https://github.com/jhonmark/mit-hrtf-lib/tree/master/source/normal .
Retain existing `docs/THIRD_PARTY_8D_SONKU_HRTF.md` attribution.
No Dear Reality proprietary IR, HRTF or DSP is used.

The 3D source-image geometry adds precisely TWO first-order reflection
paths: floor z = −sourceHeight − listenerHeight and
ceiling z = 2×height − sourceHeight − listenerHeight. It calculates
physical propagation distance, delay relative to direct path, azimuth,
elevation, measured-ear 140-tap reflection FIR and 1/path pressure.
Four original walls, native smoothing, P3 zero-spread fusion, P4 contiguous
ring reads and P5 epoch guards are reused, not duplicated.

Source height is **at listener height** until a proper measured
direct-source elevation HRTF and UI contract exist. This pilot tests
elevation of reflected arrivals; it is NOT true full XYZ direct placement.
Measured −40° is the lower dataset limit: more-negative arrivals are
clamped to −40° rather than unvalidated HRTF extrapolation. Front/rear
externalization, diffuse field and 3D position perception require
loudness-matched headphone listening with multiple people.

## Real-time discipline

- When the P7 macro is absent, 3D data and 2 extra wall processors are
  compiled OUT, preserving the public P6 legacy 4-wall renderer.
- Prepared profile is immutable across audio callbacks; illegal/unsupported
  selection fails closed. The first-image six-plane calculation runs on
  the existing 64-frame geometry cadence, not per audio sample.
- No dynamic allocation, FFT, lazy file reads, lock, new DSP worker,
  second PCM authority or listener UI placeholder.
- Same native accepted ROOM controls and audio graph; not another VST3.
  Extra two 140-tap reflection FIRs cost CPU. Measure p99, concurrent
  instances and ASIO/XRUN before considering realtime deployment.

## Gates

1. Native 44.1/48k measured-ear mirror, 0° exact original bank,
   positive and negative spectral elevation differences, physically
   causal FLOOR/CEILING delays and height validation.
2. P6 vs P7 same-source active native ROOM with 0/60 stereo spread,
   64/257 partition parity, no early ghost, finite peak and nonzero
   reflection-only difference.
3. Source-matched lossless FRONT/REAR WAV comparisons + objective metrics;
   do not call a measurable difference an audible improvement.
4. Scoped Linux CI for P0–P7 and focused Windows opt-in JUCE tests
   remain separate. Do not turn P7 on in public installer or main.

Further qualification needed: true XYZ direct interpolation,
headphone individualization, actual Windows ASIO multi-instance p99,
externalization/tonal listening and owner approval.

## P7 fast-lane observed CPU (Linux only)

Exact first P7 [Action #43555803671](https://github.com/masarray/arsonmix/actions/runs/43555803671) SUCCESS and WAV [artifact #14098845051](https://github.com/masarray/arsonmix/actions/runs/43555803671/artifacts/14098845051).
For 48k/128, 1 second synthetic PCM, Linux hosted runner: six-surface
4-insert/60° callback **p99 2581.884µs**, or **96.8% of the 2666.7µs
buffer period**, compared with roughly 1.4× P6 directional CPU in
same-runner paired modes. These are explicitly **NOT Windows ASIO
measurements** and are far too close to the deadline to justify enabling
P7 across several inserts. Do not add a naive per-buffer auto quality switch
or arbitrary CI wallclock threshold. The native host needs an explicit
preflight measured/admitted budget before P7 could be shipped.

This experiment now additionally tests P5 stale vertical FIR history on
ROOM ON → OFF while old PCM is present → ON in silence, then a new
causal impulse, at 44.1 and 48k. Whole engine must remain allocation-free.
