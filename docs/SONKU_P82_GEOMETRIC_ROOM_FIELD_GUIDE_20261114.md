# SonKu P8.2 — six-wall geometry vs late diffuse-field arrival

**Research-only, NO claim of parity with dearVR PRO 2.** P8.0 fixes a *measured low/mid ratio*; P8.2 fixes a different, measured room-time ordering defect. P7.4 cancellation, P7.5 thin/nasal timbre, and insufficient externalization remain owner reported FAILS until independently re-tested.

## Why this change has physical grounding

P8.0 objective impulse through the exact native `SonKuSpatialCore` reference (48 kHz, SQUARE 7.2 m, AZ 35°, roomAmount 65%, ambience 45%, virtual source 1.6 m) produced:
- direct first 20 ms energy ≈ **0.007308**;
- isolated six-wall early reflections energy in 20–100 ms ≈ **4.383×10⁻⁶**;
- isolated late FDN in 20–100 ms ≈ **1.751×10⁻⁴**.

That is roughly **40:1 energy in favor of late ambience** over first reflections in the same early temporal window. Thus real first wall directions can be masked. It is a *source-derived synthetic impulse observation*, not a microphone measurement, user music or dearVR capture. A solution must not simply amplify the whole reverb.

For an original hybrid room model we first preserve the six-wall image-source delays unchanged, then give the LATE diffuse FDN an additional causal propagation interval proportional to full room width: distance/speed-of-sound (343 m/s). Prepared control-side, not PCM-side:
- SQUARE 7.2 m: **21 ms** late predelay,
- CHAMBER 14.4 m: **42 ms** late predelay,
- LARGE 28.8 m: **84 ms** late predelay.

This is a **research hypothesis**, not a claim that dearVR or physical room diffusion necessarily uses the same law. FDN still has its inherent ~44ms first delay. Direct KEMAR and six-wall ER coefficients, XYZ position and prior P8.0 timbre are unchanged.

## Implementation and CPU rules

`ARMIX_SONKU_EXPERIMENTAL_P82_ROOM=ON` requires P80 timbre + P7.5 music-safe + P7.2 native XYZ opt-ins. It is **OFF in public**. Uses fixed two-channel 4096-sample predelay rings, per-sample O(1) reads/writes, and 512-frame smooth retarget across active room presets. No heap/mutex/FFT/trig/disk I/O on the audio callback. Device/restart and OFF→ON use a freshness horizon without clearing 32 KiB on the callback. Legacy default room DSP remains byte-identical.

Native GUI only shows the **NATIVE ROOM P8.2 · RESEARCH** label after a valid identity-matched actual v3 SonKu snapshot including numeric `roomLatePreDelaySamples`. No new serialized state fields or new audio authority.

## Evidence and FAT

The CI compares the same commit built with P80 vs P82 room flags, generating `sonku_P80_room.csv` and `sonku_P82_room.csv`. The C++ late-onset test asserts physical room width onset at 44.1/48k for all 3 room sizes and no stale post-reset tail. Python validates that ROOM OFF direct spectra and isolated six-wall ER are numerically unchanged, that late energy in 20–100ms is reduced (not muted), and that late tail after 100ms survives. A full Windows JUCE build (same `ARMixConsole`) with native XYZ ACK and provenance is required.

Use a headphone interface at low level. Compare baseline P7.5, P8.0 and P8.2 at matched level:
1. ROOM OFF: equal bass fullness and original stereo SIDE; any new cancellation REJECT.
2. SQUARE 7.2m: compare early arrival direction, late onset, sense of cube size vs P8.0, without treating more echo as automatically better.
3. CHAMBER 14.4m and LARGE 28.8m: perceived room size should increase with travel delay; test short transients and acoustic guitar/voice, not only sinusoidal waves.
4. Compare with independently configured dearVR PRO 2 (Clarity, Bass Boost OFF/ON, ER/late separate, wall geometry and room preset logged). A/B headphone actual acoustic judgment is **still mandatory**.
5. Test 44.1/48k, 64/128/256 ASIO and 1/2/4 inserts, WIRE and VST3, Save/Open `.smix`, restart, p95/p99, XRUN. No production admission without physical evidence.

## What P8.2 does NOT solve

This change does **not** yet implement a true stereophonic 2-object binaural matrix, calibrated frequency-dependent wall absorption, measured BRIR, room-time multi-band RT60, listener-specific HRTF, or auditory equivalence to dearVR. P8.1 and room-material research must be addressed separately. Never release based on successful sine/impulse CI alone. Original KEMAR attribution: `docs/THIRD_PARTY_8D_SONKU_HRTF.md`.
