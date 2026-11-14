# ADR SonKu P3 — zero-source-spread linear binaural reflection fusion

2026-11-14 · draft PR #674 · OFFLINE research only.

## Research and root cause
Official dearVR PRO 2 manual §2.1 documents geometric
early reflections as critical to externalization, and §3.2 ties
reflection patterns to moving source and six wall planes.
https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf
Steam Audio's implementation explicitly trades CPU against
HRTF-filtered reflection quality:
https://valvesoftware.github.io/steam-audio/doc/capi/reflections-effect.html
Neither source gives permission to copy proprietary DSP or
claims our output already matches dearVR.
The P2 renderer has 2 stereo emitter x 4 walls x 2 ears x 140 tap
FIR plus live slew per sample. 2026-11-14 CI P2 measured
~4–6x light reflection CPU on hosted Linux.

## P3 mathematically derived optimization
For exact `stereoSpreadDegrees == 0` both input emitters
travel along the identical image-source directions and acquire
identical wall path delays, gains, and 0°-elevation measured KEMAR
HRIR. Linearity gives:
`H * x_L + H * x_R = H * (x_L + x_R)`.
This holds for *arbitrary stereo*, including anti-phase material,
and does not presume L=R, add gain, or remove spectral cues.
The original stereo music-integrity side guard is upstream,
not bypassed by this optional wet-only optimization.

On each P3 room sample update both lowpass histories as before;
convolve their delayed sum only once for each of the four
image-source reflections. Advance the skipped path's HRIR filter
and delay/gain state so a live spread edit can fall back without
stale coefficients. Any nonzero spread permanently disarms
the fusion until reset. This reduces the work for zero spread,
while full 140 taps remain intact. 60° stereo spread runs
the exact P2 convolution, with no HRTF approximation.
P3 is OPT-IN through the unpublished native core accessor;
P2/alpha.23 defaults do not change; no new GUI or .smix field.

## Qualification gates
- Native 44.1/48k four input cases: true mono, independent stereo,
  inverted stereo, coherent bass + inverted treble. Compare precise
  floating wet outputs to unfused P2 on a rotating moving source,
  assert RMS <3e-6, worst-case absolute <2e-5. Verify 60° spread
  and return to 0 remain unfused after initial nonzero spread
  until reset, with no stale-filter spikes.
- Same-runner 48k/128/3s P2-vs-P3 benchmark with *spread 0*,
  plus a separate 60° control. No performance claims until CI.
- P0/P1 music baseline 6 WAV, P2 4 WAV and KEMAR 380
  spectral checks all remain required. No release/merge pending
  headphone A/B and Windows ASIO live CPU gates.

## Known limits
Optimization works only for EXACT zero source spread; it
does NOT reduce CPU on moving 60°/180° source-separated stereo.
No six-surface geometry, multi-elevation measured HRTF, headphone
individualization, head-tracking or true BRIR is claimed.

## Native same-runner performance observation
GitHub Actions #43533510068, 48kHz/128 samples/3s:
P2 spread 0 CPU **263.289ms**; P3 spread 0 **178.636ms**
(1.474x faster), p99 **250.590us -> 176.808us**.
P3 with spread 60 explicitly does not fuse (248.051ms).
These numbers are NOT full Windows ASIO/graph deadlines.

## Additional audio gate
Render four original-source 2s PCM WAVs: P2 reference and P3
fused, FRONT and REAR. Same exact 140-tap horizontal KEMAR,
room geometry, direct HRTF, 48kHz and 16-bit conversion.
Require <=2 peak PCM LSB, <2e-5 RMS, <0.05dB on
165/660/2200/4700Hz in both ears. This evaluates
sound quality invariance, not externalization superiority.
