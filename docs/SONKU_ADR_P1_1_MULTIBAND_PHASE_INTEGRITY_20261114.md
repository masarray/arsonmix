# SonKu ADR P1.1 — mixed-polarity frequency-local integrity

2026-11-14 · experimental draft PR #674 · no public release.

## Source-proven failure and decision
The single JUCE native PCM `SonKuSpatialCore` maps L/R into two
measured KEMAR HRIR sources, summed at each ear. At zero source
spread, L=-R in *any frequency band* cancels in both ears. The
existing P0 full-band correlation guard recovers only globally
anti-correlated input. A stronger centered 170/220Hz bass can
hide an inverted 3200Hz instrument, leaving global correlation
positive and the high-frequency signal destructively lost.

Inside the **existing** callback-owned `SonKuStereoIntegrity`,
split each input with prepared 260Hz and 2600Hz one-pole filters:
`low=LP260(x)`, `mid=LP2600(x)-LP260(x)`,
`high=x-LP2600(x)`. These three bands sum to the original
input sample for sample (no linear-phase FIR, lookahead or new
graph). Each has its own 20ms correlation evidence, -0.78/-0.55
hysteresis and 15ms de-click blend. Preserve only locally
inverted stereo difference; the existing broadband gate remains
authoritative when full-band L=-R, to avoid double rescue.
The P0 positive coherent MID HRIR correction remains unchanged.

## ABI/performance/risk
Only small fixed POD state and per-sample add/multiply operations.
No allocations, locks, file I/O, network, runtime coefficient
design, FFT, extra convolution or GUIs. The original source
side-bed bound of 0.65×mix×trim, `.smix` v1/v2,
native ACK/readback, routing, FX identity and JUCE graph remain.

A post-HRTF stereo side bed is partly **head-locked**, not
physically externalized. Band crossover changes can affect
timbre/image stability. A passed unit test is only proof of
synthetic signal preservation, NOT externalization or dearVR
equivalence. Reject the experiment if subjective A/B shows
unacceptable artifacts. Subsequent milestones require directional
BRIR, personalizable licensed HRTF, distance DRR and real ASIO
CPU/headphone evidence.

## References
- Official dearVR PRO 2 manual (documented room/distance/Clarity
  product behaviors, not proprietary code/coefficients):
  https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf
- Sennheiser official timbre-vs-localization discussion:
  https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2
- SOFA/AES69 (future data format): https://www.sofacoustics.org/
- 2026 BRIR/early reflection externalization evidence:
  https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/
- ITU-R BS.1116-3 listening method:
  https://www.itu.int/rec/R-REC-BS.1116-3-201502-I/en

SonKu's frequency-band decision is original experimental
filter/correlation algebra, not a claim about dearVR internals.

## Verified first-pass GitHub Actions evidence

Following exact digital-silence repair, [Actions #43524526143](https://github.com/masarray/arsonmix/actions/runs/43524526143)
passes native 44.1/48k source regression, six identical-input baseline/candidate
WAV pairs, 380 KEMAR spectral checks and runner-local CPU benchmark.
The sixth 3200Hz note recovered above an objective amplitude gate of
0.006 while the coherent 220Hz bass changed by less than 0.25dB.
This is a machine test threshold, not a perceptual listening rating.
The run's artifact is [#14083314097](https://github.com/masarray/arsonmix/actions/runs/43524526143/artifacts/14083314097).
