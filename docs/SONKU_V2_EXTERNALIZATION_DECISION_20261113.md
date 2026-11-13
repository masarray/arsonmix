# 8D-SonKu V2 — Externalization & Natural Orbit Architecture
2026-11-13 | Experimental stage M1 | Based on PR #606 baseline

## Confirmed user observations
- Headphone listener reports correct directions, 4/4 known and 8/8 blind.
- FRONT/REAR are too thin/near-head and motion through both median-plane axes lacks realistic depth.
- This is a perceptual report, NOT a measured distance, CPU benchmark or proof of identical HRTF fit for all listeners.

## Technical research and prior art (no code or skin copying)
- Allen & Berkley (1979), Image Method for Efficiently Simulating Small-Room Acoustics, JASA 65(4), DOI 10.1121/1.382599 — geometrical mirror-image first-order acoustic paths and time-of-flight.
- Steam Audio core guide: https://github.com/ValveSoftware/steam-audio/blob/master/core/doc/guide.rst — direct binaural HRTF, room reflections and distance effects are separate, testable stages.
- 3D Tune-In Audio Toolkit: https://github.com/3DTune-In/3dti_AudioToolkit — ITD-independent HRIR interpolation and acoustic parallax are mature solutions, *research for later M2*, not implemented here.
- Frontal externalization BRIR study: https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/ — altered early reflections and direct/reflected ratio can improve front externalization for some listeners, not a guaranteed result on all headphones.

## Current implementation decision: split M1 from the other acoustic stages
Keep SonKuSpatialCore and native JUCE/PCM graph as one authoritative processor; no new DSP engine, new device callback, JS audio path, thread, blocking call, file access or dynamic callback allocation. Add SonKuEarlyReflections as a fixed-memory, first-order image-source *experimental* room stage. Amount=0 by default and guarantees identical previously qualified native PCM. Amount=.55 generates *offline-only* A/B comparison WAVs using the exact original source samples and direct KEMAR HRIR. Do not publish this as a new default until blind headphone tests support it.

### Bounded acoustic model (M1)
- Room width 6.4m, depth 8.0m, listener centered, 2 sources at 1.6m rotating on the horizontal ring.
- Four first-order wall images per source (left, right, front, rear), hence eight reflected paths.
- Delay in samples = (distance from image - direct source distance) / 343m/s × sample rate, plus a small symmetric ear baseline to create directional ITD; gains = wall reflectivity × directDistance/pathDistance × fixed safety factor.
- Mild source low-pass, ear-directional ITD/ILD approximation; NOT HRTF-convolved reflected signals or FDN reverb.
- Fixed 8192-frame power-of-two ring for each source (~64KiB total) and fractional-delay interpolation; geometry updates every 64 frames while delay and gains smoothly follow per-sample.
- Works at 44.1/48k; conservative fixed delay bounds; original direct HRTF FIR, stereo dry, spatialMix, outputTrim, ASIO and native ACK/readback remain untouched.
- One C++ unit gate, one exact-core integration parity test, two A/B WAV comparisons and a bounded CPU comparison before Windows FAT.

## Staged next steps (NOT yet delivered)
M2: ITD/time-aligned measured HRIR preprocessing + fractional ear delay, grid-point identity and phase/ILD regression; no generic "phase aligned" switch without proving source accuracy.
M3: if needed, prepared HRTF-filtered reflections with bounded convolution and room distance parameter.
M4: only after successful listening trials, a compact SPACE control with native typed lease, accepted ACK/readback, versioned session schema and a calibrated default. No extra user-facing page.
M5: optional low-cost FDN late reverb, several HRTF profiles / calibration and head tracking after measured CPU and perceptual evidence.

## Listening gate
Compare the legacy source-accurate 4-direction WAV and 10s orbit with two V2 files rendered from EXACT same input samples. Match perceived level, listen on headphones with OS virtualization off. Measure 8-direction mapping, front/rear distance, orbit continuity, colorations, metallic echoes and time-varying flutter. If room worsens direction or sound quality, keep amount=0. CI PCM PASS alone does not prove spatial externalization.

## 2026-11-13 listener feedback and two-turn orbit gate
Listener recognizes direction but describes a noncircular perceived orbit: RIGHT → FRONT is pleasing, yet FRONT/RIGHT/REAR continuity feels incomplete and FRONT subjectively nearer. The room is **rectangular 6.4 × 8 m**, so reflected image distances and directional/temporal profiles vary across axes. This is one plausible contributor, not a proven cause; non-individual HRTF and ITD-inconsistent HRIR interpolation remain other hypotheses.

The offline evaluation now renders a **single uninterrupted 20-second, 2 × 360° clockwise trajectory** (10 seconds/turn) for both legacy direct HRTF and rectangular-room A/B. Only fade in at time 0 and fade out at 20 seconds; no fade/reset/offset at the 10-second boundary. Landmarks: FRONT 0/10/20s; RIGHT 2.5/12.5; REAR 5/15; LEFT 7.5/17.5. PCM tests verify 960,000 frames, non-silent seam, fixed rate and output sign, and C++ end-of-two-turn phase closure. Do not market a verified circular perceptual effect until headphone evaluation.

## 2026-11-13 — rectangular vs symmetric-room controlled comparison
The report of an elliptical/unequal-distance orbit does NOT prove a room-model bug. Alongside V1 direct HRTF and the existing rectangular 6.4×8m image-source room, a third A/B WAV uses a square 7.2×7.2m room with all wall reflectivities identical. The emitted direct HRTF, sample clock, source waveform and 2×360° motion remain identical. The square wall layout has fourfold rotational geometry and tests whether wall distance contributes to perceived asymmetry; it is NOT a circular room or a promised perceptual cure. C++ tests check left/right mirror of the reflected impulse. A/B only, original native default remains OFF.

## 2026-11-13 — independent reversed-motion listening probes
In response to asymmetrical perceived naturalness of RIGHT→FRONT vs FRONT→RIGHT, add three CCW two-turn WAVs alongside the three clockwise versions. All six are 20 seconds, use the exact same deterministic original noise, same native HRTF and DSP sample clock, and have no loop seam. Clockwise: FRONT → RIGHT → REAR → LEFT → FRONT. Counterclockwise: FRONT → LEFT → REAR → RIGHT → FRONT. This allows listening comparison of RIGHT→FRONT (CCW at 7.5–10 and 17.5–20 seconds) with FRONT→RIGHT (CW at 0–2.5 and 10–12.5 seconds), and tests whether perceptual asymmetry reverses with source rotation direction. No additional live motion modes, no UI changes, no assertion of physically circular perception.

## 2026-11-13 — listener-approved Square Orbit checkpoint and musical late reverb M2
The owner has listened to and approved the **square-room orbit as perceptually circular**. Its exact base code is pinned at `checkpoint/8d-sonku-square-orbit-approved-20261113` (`3a74b0273c7a7e13d5d2614183d19995eb12ad57`); this immutable evaluation milestone must not be overwritten by reverb experiments. There already are eight bounded 1st-order wall-reflection paths (four per source), but **there was no late reverb**.

New *opt-in offline candidate*, original `SonKuMusicalReverb.h`: 8 noncommensurate delays with a normalized Hadamard orthogonal feedback matrix, per-line RT60 feedback gains prepared for 44.1/48k, damping low-pass in the loop, stereo decorrelated output, 12ms wet ramp, and at least 43ms to first late energy. Max delay ring 8×4096 floats (~128KiB) with no dynamic PCM buffer, FFT, allpass resonator cascade, or per-sample trigonometry/exp. Two listening-only presets: Studio Glue (RT60 ~0.78s; 5.9kHz damping) and Airy Chamber (RT60 ~1.28s; 7kHz). Wet amount .42 for controlled A/B is intentionally modest and **OFF in the native plugin**. Square early reflections remain exactly as approved, with no room-size or HRTF clock modification. Compare baseline square with Studio Glue and Airy Chamber 20s 2-turn WAVs, using same source, latency, level trim, azimuth and direct HRIR. C++ tests validate stable impulse energy, late onset, stereo width, RT60 decay, byte-exact disabled state and PCM partition parity. No GUI knob, session upgrade or public release until listener feedback and physical Windows audio qualification. FDN theory: Stautner/Puckette (1982), Jot/Chaigne (1991), and practical low-metallic density guidance from Valhalla DSP; original GPL-compatible implementation, no copied commercial source.
