# 8D-SonKu P7.5 — real-music cancellation incident response and physical FAT
14 November 2026 · **Research only · previous P7.4 field audio REJECTED**

The owner has reported severe cancellation and destroyed sound during physical listening of the P7.4 Windows XYZ tester. Do **not** install that candidate for live monitoring, play out at high volume or claim dearVR parity. P7.5 is an isolated diagnostic render experiment; it neither replaces the release branch nor grants production admission.

## Why the previous tests missed the failure
Previous P0–P7.4 fixture tests proved anti-phase energy only for a few strong coherent/inverted sinusoids. The actual stereo algorithm rendered left and right program channels as two spatial HRIR objects and summed each ear. MID compensation was correlation-gated and SIDE was restored via an unaligned, adaptive direct path. Partial/stochastic music coherence, stereo stems and dynamic spectra were not proven stable. The Linux objective CSV/WAVs could not certify subjective music quality. The owner’s physical evidence invalidates the marketing-level readiness claim.

## P7.5 hypothesis — isolated stereo music-safe test mode
- Compile only when `ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT=ON` **and** `ARMIX_SONKU_EXPERIMENTAL_MUSIC_SAFE=ON`; default public and original P7.4 remain OFF.
- MUSIC MID `M=(L+R)/2` is rendered by **one centered measured MIT KEMAR HRTF** (both ears). This avoids cross-summing two offset impulse responses for the same musical object. Elevation, azimuth and measured room remain from the same JUCE native PCM instance.
- SIDE `S=(L-R)/2` is never correlation-gated. It is delivered symmetrically to the headphone ears with a bounded width gain: at width 0° → 0.5×; 60° → 1×; 90° and beyond → 1.25×. Full inverse-polarity music therefore cannot produce the previous zero-pressure null. **This side is still inside-head** and is NOT a substitute for fully externalized individual stereo sources. Width is an M/S stereo image width control in this experimental mode, not two independent physically located monitors.
- ROOM/late stereo inputs are based on the musical MID; they cannot sum an inverted SIDE into a false spatial reflection. The SIDE bed remains clean and independent.
- Coherence-triggered gain modulation is skipped for the experimental render, reducing dynamic image pumping. The direct FIR loop runs only two measured-ear MACs per sample/tap rather than four. No new callback worker, async CPU probing, heap, mutex or WebAudio authority is added.
- True output tonality, bass, transient behaviour, 1-channel vs 2-channel layouts and externalization are **not yet qualified**. Even with these guarantees, mixed dry/HRIR at nonzero intermediate wet percentage can cause coloration and requires real musical listening.

## Validation gates
1. P7.5 source tests: 44.1/48k; FRONT/REAR; anti-phase with SPREAD 0/60/180, exact side transfer at ROOM OFF; mono MID identical for any SPREAD; mixed bass MID + inverted upper band; ROOM XYZ+motion partition 64/257 parity, finite/headroom.
2. Full Windows JUCE 8.0.15 `ARMixConsole` compile with the new flag; actual JUCE XYZ/ACK/v3 state tests; portable PE and bundled GUI hash check. Source commit + music-safe mode must be in provenance.
3. Before full-volume listening: use low headphone volume, ROOM OFF, ORBIT OFF, stereo music file with both L/R, SPREAD 60, SPATIAL MIX 100%, GAIN at -6 dB and compare **bypass → old P7.4 → new P7.5**. Capture L/R dry and new wet output for FFT/rms phase transfer. Stop on severe cancellation/bursts.
4. Once good, evaluate ROOM and motion, source elevation, room on/off, 44.1/48k, 64/128/256 and 1/2/4 concurrent inserts with ASIO/WIRE/VST3; save/open `.smix` and restart device.
5. Need stereo music, hard-left single channel, duplicated mono, phase-inverted stereo and correlated narrow stereo as distinct cases. If hard-left single channel is unnatural in this experimental M/S presentation, **record and reject** rather than silently deem it fixed. Subsequent product architecture must explicitly handle mono-vs-stereo routing semantics.
6. Loudness-match to dearVR PRO 2 with original licensed music, compare side width, tonal accuracy, front/back, elevation, real room depth and perceived externalization with blinded headphone tests. Do NOT certify equal quality from one engineer or a green CI.

## Safety and release
Original P7.4 portable should be considered an unsuccessful audio test; P7.5 is a candidate to investigate the failure. Public `main`/alpha installer and default LIGHT must never be changed based on these offline tests. Hardware and owner approval are prerequisites to promotion.

Source provenance and redistribution conditions: `docs/THIRD_PARTY_8D_SONKU_HRTF.md`.
