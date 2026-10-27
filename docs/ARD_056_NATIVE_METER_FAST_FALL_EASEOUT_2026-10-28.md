# ARD-056 P3 — Fast fall, smooth landing for real native peak LEDs
Base: main de683b001a5a1e5e050bc20ddb52d32b22fa58d1 (includes PR #104 real MASTER L/R visibility, PR #105 capture/dearVR fixes). Independent of open FX Reorder PR #102, GUI PR #97 and CI PR #101. Scope **only JavaScript native meter visual ballistics**.

## Why the old motion felt wrong

Old `nativeMeterStep(level,target,fallVelocity,dt)` started the downward velocity at 14 level-units/s, then **accelerated** at 230/s² until terminal 100/s. The first frames were almost static; speed grew *near the bottom*. Owner wants the opposite: immediate dramatic visual descent after a signal drops, with controlled slowing as LEDs approach the measured output. That is **ease-out**, colloquially 'reverse gravity', not true constant-gravity free fall.

## Design choice

One-pole exact exponential envelope in display level-percent space only:
`next = target + (display - target) * exp(-dt / tau)`.
Choose `tau=18ms` when audio target rises (attack); `tau=165ms` when target falls (release). Large initial display–target gaps produce the highest initial fall speed, and speed naturally decreases to zero. This preserves monotonic movement, never overshoots or bounces, and is nearly frame-rate invariant even on busy standard GPUs. Snap when the residual is less than 0.3 level units, below an LED cell. Clamp elapsed time to 65ms to prevent one big jump after scheduling stalls.

At 34ms step for a 96→8% signal drop: `96 → 79.6 → 66.3 → 55.4 → 46.6 → 39.4 → 33.6`. Compare the old first step near 95.3%, after which it accelerates down. Transient attack from 8→96 reaches ~82.7% at the first 34ms step. These are pure envelope calculations, **not guaranteed observed GPU FPS or real peak measurement accuracy**.

- Preserve existing 34ms shared timer + single rAF (~29.4 scheduled frames/s), no per-strip clocks.
- Scalar function returns a number instead of allocating `{level, velocity}` per meter per frame. Native states now store only `level,target`; no velocity integration or allocations.
- Keep `setLedMeterLevel()` discrete fixed 7px LED quantization and dirty-write suppression, stable green/amber/red palette, independent MASTER L/R, and actual native `peakLeft/peakRight` data; no fake animation source or generated sample.
- Preserve direct-mode `?metermotion=direct`, reduced-motion, tab hidden synchronization and detached DOM pruning.
- Do not touch JUCE C++ audio callback, 12Hz telemetry, Orbit, VST3 FX, master FX DSP truthfulness or BTN-180 skin.

## Professional meter semantics and limits

This is a **visual peak activity bar**, not a calibrated IEC Type I/II PPM, VU, LUFS/true peak meter or proof of inter-sample overs. A fast-decay decorative bar should not be used as a clip detector or numeric loudness measurement. Keep independent real peak/clip markers; future standalone instrument grade calibration can use a calibrated dB/s decay profile with selectable hold and true peak oversampling, driven by actual DSP measurements. No brightness/glow transitions or fake smooth continuous filled bar.

## Deterministic acceptance

Browser test covers attack/release ordering, rapid initial descent and monotonically shrinking fall steps, exact time-split equivalence (2×34ms vs 65+3ms), no overshoot, target equality, near-target snap, retarget on new transient, 65ms delta clamp, and shared 34ms scheduler contract. Node enforces numeric step owner/no velocity allocation. Standalone app.js embedded parity. Chromium+Firefox UI regression including native MASTER LED visibility; Windows MSVC CTest and package; postmerge CI exact main before artifact release.

**Remaining physical gate:** Listen to actual routed audio and inspect CH1 vs MASTER L/R against true real signal on low-spec PC with FFT/audio loopback as available; compare `metermotion=direct` vs default for rendering feel and p95/p99 pointer frame intervals, note 12Hz telemetry itself can alias sub-83ms transients unless DSP holds peak per callback. This update does not promise validated metrology or real-world FPS.
