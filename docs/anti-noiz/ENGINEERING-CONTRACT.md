# Anti Noiz — maintainability and optimization contract
Owner direction (2026-11-25): maintainable and optimized from the first implementation; no speculative patch stacks. ASKP-VST DSP may be reused and is intended to become open source. This does not authorize publishing the entire private repository or secrets. Preserve the existing ASKP release boundary until explicitly changed. Review upstream file ownership and third-party licensing before copying code; record exact provenance and applicable license. No changes to ASKP itself in this PR.

## Reuse decision (source audit)
Reference ASKP DenoiseEngine.h, reviewed at immutable upstream revision. Reuse proven concepts or extract a small audited pure DSP component only when superior under the same regression/benchmark harness. No product wrapper dependency or second DSP renderer.
- Fixed FFT/OLA, delay-aligned unity, shared spectrum: keep existing tested Anti Noiz path; extract reusable transform plan instead of maintaining competing FFTs inside Anti Noiz.
- ASKP gain estimator: compare tonal/transient protection and asymmetric time constants against the independent baseline; do not copy per-bin pow/exp or atomics in inner loops. Prepare coefficients and immutable parameter packets once. Test objective distortion and active-noise suppression.
- ASKP voice confidence: useful baseline features, not a proven speech/music classifier; no promotion to authority until mixed/singing/music corpus demonstrates low false attenuation.
- ASKP smooth expander: benchmark soft knee, hysteresis, hold and two-speed closure against residual baseline; use linked raw-input evidence plus residual estimate; preserve weak consonants/tails. Avoid per-sample log/pow where a bounded, tested approximation is justified.
- ASKP voice enhancer and product wrapper: exclude from Anti Noiz. No coloration, unrelated DSP, payment, update, licensing or 8192-sample scratch copy as a default.
- Shared library future: one source-of-truth host-neutral C++ DSP module, separate host adapters. Pin an approved revision only after both product tests and license audit; do not make ArSonMix depend on private repo access.

## Required architecture
Core owns audio histories; fixed FFT plan is a separate primitive. Next split noise profile (transactional candidate/trusted), feature extraction, context policy (pure, testable), gain estimator, residual expander, and stereo coordinator at natural ownership seams, not one class per trivial function. No cyclic dependencies or virtual dispatch in per-bin/per-sample inner loops. Native JUCE adapter owns lifecycle/typed state, control commands and actual audio-applied ACK; GUI only consumes bounded telemetry. Keep one native graph and existing /preview/ ownership. No duplicate parameter store or worker renderer.

## Performance and quality rules
1. Establish exact-SHA baseline first. Isolate one causal hypothesis and a reproducible test; compare matched noise attenuation and level-matched audio. Reject tuning based only on synthetic tones or a single microphone.
2. Allocate, validate, derive coefficients/FFT plans on prepare/control. Fixed arrays and bounded work in callback; no blocking, allocations, I/O, UI, network or unbounded retries. Snapshot coherent settings once at a safe boundary.
3. Share analysis, avoid redundant transforms; fast paths must preserve latency, state transitions and audio integrity. Keep detectors active when Auto policy needs them. Do not claim transparent mode saves CPU without measured evidence.
4. Measure P50/P99/max callback, allocation count, real audio quality, stereo and latency on 32–1024 frame blocks and supported rates. Investigate >10% regression on relevant reference workload; no arbitrary CPU guarantees from a Linux microbenchmark.
5. Each change must record owning layer, root cause/design rationale, rejected alternatives, tests and known unproven paths. Three-patch circuit breaker: stop symptom chasing and re-audit architecture before a fourth patch.
6. No hidden product admission or release; use exact-SHA native/GUI tests, Windows/ASIO FAT, approved integration gates. Preserve other FX and mixer.

## AN-01B tested refactor
Core blob 0da5562510811b0846f53587f41ec4be56e31755; FFT blob 9cb0983045665a98ba51a73992edfc76e759a48e; existing test blob de641f2054897b5a77f9cb11298d55914cc84106. Local hash equality verified. Linux C++20 -O2 -Wall -Wextra -Werror regression PASS; previous synthetic results unchanged (36.167 dB idle, 1.81287 dB tone proxy error improvement, gain 1.00011). Extracted transform plan; cached gain floors and rate-derived smoothing/hold; unity WOLA path avoids both forward/inverse FFT when no profile or explicit Music and not learning. Same delay, not bit-identical to FFT reconstruction; existing tolerance and block-partition tests pass. No new classifier or active-quality claim.

Microbenchmark: 48 kHz stereo, 64 frames, 60k blocks, 3 alternating runs, Linux container, seeded repeated noise, includes copying and checksum. Median elapsed ms:
| mode | baseline | refactor |
|---|---:|---:|
| No profile | 441.81 | 55.727 |
| Music with learned profile | 452.004 | 59.163 |
| Voice with learned profile | 472.664 | 462.614 |
Large run-to-run variability; transparent path improvement is promising; no reliable active-mode speedup claim. Not P99, ASIO, hardware or full mixer timing. See benchmark source; no heavy CI triggered. Next: calibrated context, real corpus, active gain quality and independent expander before native integration.
