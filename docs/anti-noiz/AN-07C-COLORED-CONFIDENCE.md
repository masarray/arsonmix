# AN-07C — colored-noise confidence experiment
Baseline: ff24b1c93a84dc4c8c10980eba50d35365e26315 (AN-07B). This stack must not alter #900 or #902 during native qualification.

## Audit and hypothesis
Raw posterior SNR drives the decision-directed onset override and the confirmed-idle spectral mask. A random excursion in a dominant learned band can be mistaken for desired signal. Reuse the existing 45ms temporal neighborhood and anchor-relative noise energy to condition moderate, isolated excess only in explicit/source-aware Voice. Keep the original strong-onset authority, quiet-band reserve, stereo union, profile transaction, delay, Clean/Silence settings and non-Voice behavior. No new FFT, allocation or state; arithmetic and an existing neighborhood lookup only. This is a heuristic, not calibrated speech presence probability or a proven classifier. It does not change the idle detector itself.

Research motivation: Cohen, "Speech enhancement for non-stationary noise environments" (2001), https://www.sciencedirect.com/science/article/abs/pii/S0165168401001281 ; correlation of foreground presence in neighboring time-frequency regions. Independent implementation, no external code reused.

## Qualification
Direct bounds assertions in existing Voice test, full existing regression and sanitizer suite; paired pinned corpus build on frozen baseline and candidate, with exact row identity and source match. Report every active, weak, idle and per-channel metric. Experimental non-regression gates: full-active error delta >= -0.5dB, absolute projection-error increase <= .02, idle delta >= -1dB; non-Voice parity tolerance .001dB and .0001 projection. Existing transparent checks and target flags unchanged. These tolerances are proposed experimental gates, not acceptance of target misses. A reduction in flags is not enough by itself. Review per-case gains and weak detail, repeat RT and native tests on exact candidate, then matched listening and real Learn contamination. Do not merge/release based on this experiment alone.

## Status
Candidate authored; results pending exact-head CI. No local/full native/hardware qualification claimed. No new idle detector authority. If active gain is insufficient, next step is learned window variability and foreground-vs-noise idle evidence, not a blind threshold increase.
