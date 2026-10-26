# P0-09 — Deterministic independent-clock ASRC continuity + performance evidence

**Checkpoint**: `main@907d404cad1a961e43736d4ddea0b89a169b2f6f` (P0-08 stream-format fence merged).
**Scope**: a reproducible, hardware-independent **validation harness** for the *existing live* `AdaptiveClockBridge`. DSP, callback topology, GUI and driver configuration intentionally unchanged.
**Release status**: DRY 0-FX hardware FAT remains OPEN under [issue #15](https://github.com/masarray/arsonmix/issues/15).

## What is actually tested

`tests/adaptive_clock_virtual_soak_test.cpp` feeds the same production `AdaptiveClockBridge` with **two independently scheduled virtual sample clocks**, avoiding OS sleeps or mock resamplers. Six deterministic profiles, each 8 seconds of generated audio, cover:

| Capture → render | Clock drift | Capture scheduling disturbance |
|---|---:|---|
| 44,100 → 48,000 | +300 ppm | seeded short jitter |
| 48,000 → 44,100 | -300 ppm | seeded short jitter |
| 48,000 → 48,000 | +100 ppm | seeded jitter + periodic 30ms stalls |
| 48,000 → 48,000 | -100 ppm | seeded jitter + periodic 30ms stalls |
| 96,000 → 48,000 | -300 ppm | seeded jitter + periodic 30ms stalls |
| 48,000 → 96,000 | +300 ppm | seeded jitter + periodic 30ms stalls |

The capture oscillator produces **integer-indexed PCM** from rational rate/drift expectations, independent from the render oscillator. Jitter temporarily **delays delivery without inventing or deleting captured frames**; when a delayed capture callback resumes, all due frames are pushed in bounded 512-frame chunks. A preallocated 32,768-frame ring is configured with an explicit 70ms target for the test matrix. The render callback uses a changing advertised quantum of 64, 128, 256, 480 and 512 frames. Importantly, variation of normal callback block size is supported; P0-08 validates *negotiated quantum in device lifecycle*, not the number of samples per callback.

**Correctness gates**: exact accepted capture frames / rendered frames, no capture overflow or post-warmup priming/underflow under these target/stall assumptions; bounded FIFO fill and ±2,000 ppm PI correction; finite audio after intentional NaN capture ingress; peak bounds, RMS window for separate 997Hz/1741Hz stereo tones, low cross-correlation to catch channel duplication, and bounded adjacent-frame discontinuity. Existing native `AdaptiveDualClockBridge` CTest independently tests 1 kHz passband versus 19 kHz anti-alias attenuation at 48 → 32 kHz and deterministic 60ms capture starvation -> continuity failback, so this harness complements rather than duplicates it.

**Performance evidence**: each test prints a machine-readable `ASRC_SOAK` line with simulated seconds, actual test execution time, callback count, p95/p99/p99.9 **host process-time percentage of nominal callback period**, stereo RMS, maximum sample step, occupancy, correction and exact FIFO losses. The percentile values are **observations only**, not a flaky CI time threshold nor evidence of physical DAC underruns. They are not embedded in the realtime engine and do not invoke a high-frequency UI logger.

## Verification and CI economics

- New isolated CTest `IndependentClockASRCDriftJitterSoak` with a 30-second execution watchdog; all tests use Release C++20 MSVC in the **existing single Windows CI job** and new binary builds in the existing `--parallel 2` target list.
- Node source contract ensures the physical bridge and test assumptions are included; no new dependency, unrelated CI workflow or third-party audio framework.
- Only merge if exact head Windows native MSVC + all CTest, native JavaScript and Chromium GUI smoke are green, with no unresolved review comments.
- This is a **48-second aggregate virtual audio stress baseline** (six 8-second cases), not the long-duration ≥60 minute simulated drift specified as the aspirational full qualification suite. Full 60 minute FIR runtime under representative rates could be CI-expensive; use a separately designed and profiled offline soak before promoting production profiles, rather than mislabeling this suite as satisfying that gate.
- Compiler/runtime tests demonstrate **functional behavior and bounded memory**, not physical audio quality under Bluetooth/device scheduling, real DPC interrupts, actual Windows driver render underruns or measured loopback latency.

## Follow-on work

Use the reproducible results to investigate any observed loss or pathological correction before tuning. Continue with ASIO hardware identity and genuine physical render provenance, then a bounded AUTO/STABLE coordinator using verified profiles. Avoid lowering physical buffer targets merely because this virtual test is green. One consolidated supported-hardware 44.1/48k, 0 FX then VST3 FAT is still required.
