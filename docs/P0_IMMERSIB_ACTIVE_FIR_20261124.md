# P0 active ImmerSIB and interval evidence — 24 Nov 2026

Independent development branch at INT-1 eade9fc6. Do not edit or redispatch #886 merely for this work. Prior isolated EARLY p99 ~676us vs FDN ~12us, not additive. Real field reproduction i5-7300U / 48k / 480 frames, silent second instance power off restores audio. Existing sleep and zero-latency host fast path retained.

Candidate: hoist stationary non-fused contiguous FIR dispatch out of the per-tap loop, preserve fractional interpolation, sequential sums, fresh bounds, kernel stasis and all fallback paths. No coefficients, presets, tap reduction, SIMD/FFT, fast math, driver, added latency or memory. Executed in temporary test overlay ONLY; approved 30 acoustic files and existing source/provenance lock unchanged.

Actual three-way full-core PCM bytes original/current/candidate, seven scenes/two rates/room edits/ring wrap/anti-phase/silence. Paired benchmark 12 cases, 1/2/4 active, 44.1/48k, 128/256/480f, scenes 2/8, 192 measured blocks after 128 warmup, two order-alternating repetitions. All measured samples fingerprinted; PCM drift fails. No timing threshold on shared runners; require consistent improvement vs CURRENT, not older original. Focused MSVC parity and real i5/PS22 FAT required before admission. Tool --msvc supported in VS developer shell, no Windows dispatch here.

Native lease adds monotonic total processBlock us, processed frames and sleeping frames; single writer under existing gate uses relaxed load/store rather than locked RMW. Independent observations, not coherent per-block snapshot. Compare same instanceId and epoch; no XRUN/instantaneous CPU inference. Real JUCE regression asserts exact frame deltas and frozen total while sleep. Windows native regression pending. No new UI.

P0 delivery: review measured result, promote only if no PCM change and repeatable benefit; otherwise retain current core. Reconcile isolated branch with actual #886/main after candidate qualification. Single-ASIO physical FAT pending; multi-device deferred. No release claim.

## Production admission decision — blocked, rollback to qualified source

- Windows MSVC scoped A/B run [45380667060](https://github.com/masarray/arsonmix/actions/runs/45380667060): all 12 branded PCM cases bit-exact; ~52% P50 reduction; zero candidate over-budget blocks in the test.
- Promoting exact candidate blob in 4d88c1bdbb5d1846b04c1bf2dd19cb011c71851b exposed a separate legacy P9.8 golden failure in [45381307414](https://github.com/masarray/arsonmix/actions/runs/45381307414/job/138400730704), at the existing absolute PCM hash assertion. The branded benchmark and legacy golden exercise different compile configurations. Root cause remains unproven.
- Restore exact previously qualified production source, source-lock manifests and benchmark overlay from 199a417fb7d501b2b710ffadaa62ca5d79f9c48d. No golden values changed; no acoustic rebaseline, no production admission. Keep the candidate as experimental evidence only. Next gate: demonstrate candidate parity under the legacy golden compiler flags and full product configuration, then verify real native host and hardware before promotion.
