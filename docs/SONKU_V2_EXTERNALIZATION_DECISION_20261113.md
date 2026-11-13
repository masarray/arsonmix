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
