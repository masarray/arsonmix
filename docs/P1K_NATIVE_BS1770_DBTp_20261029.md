# P1K — Native measured 48 kHz MASTER LUFS-I and dBTP (2026-10-29)

**Scope:** Native JUCE DIRECT output, 48 kHz and first two physical output channels only. This PR does not certify all device/rate modes or crackle-free physical audio.

## Native signal authority
- Analysis reads **real hardware-bound MASTER PCM after fader, MUTE, routing, FX and ramp**, both from the existing CH1 path and the approved 10CH DRY Routing LAB. It never reads the browser preview meter or substitutes sample-peak telemetry for true peak.
- `engine/dsp/NativeMasterLoudness.h`: two reference 48-kHz ITU-R BS.1770 K-weight biquads, 400ms/75%-overlap blocks, -70 LUFS absolute gate and -10 LU relative gate; fixed 0.1 LU histogram quantization (maximum nominal 0.05 LU quantization).
- Native dBTP is measured from 4x interpolated audio via 32-tap Blackman-windowed sinc polyphase FIR, with DC normalization and an absolute maximum since device epoch. Intersample peaks are detected separately from the existing sample peak display.
- Always initialized **before** audio callbacks. `process()` has fixed-capacity buffers, no locks/allocations/VST/window/file/OS operations. Callback storage is O(samples) with bounded FIR work. Message-thread `read()` publishes provenance-tagged fields at the existing 12Hz status cadence; 10CH + AUX DSP topology and FX/Orbit callbacks remain unchanged.
- LUFS-I starts only after at least 400ms of valid above-gate signal; offline, silence, disconnected, and unqualified sample rate display **—**, not made-up numbers. dBTP holds maximum observed since current device start.
- Real JS WebView2 receives `masterAnalysisProvenance="MEASURED_48K_BS1770_K_4X_FIR"` and finite `masterLufs`/`masterDbtp`; the compact two-row black LCD displays numbers in bright green, changing to red only for LUFS-I above −12 or dBTP above −1. Browser standalone keeps its clearly simulated demo.

## Limitations/gates
- INTENTIONALLY unsupported: 44.1/88.2/96/192k until recalibrated K weighting and oversample spectral acceptance; independent WASAPI dual-clock output not yet instrumented (cannot silently reuse the DIRECT capture callback); output multichannel >2 not qualified. When unsupported, native LCD remains unavailable.
- Loudness histogram is fixed-memory and approximates exact floating-point gated block list by 0.1 LU. Compare against known ITU vectors, silence, stereo vs mono and live pro tooling before asserting broadcast certification.
- 32-tap 4x Blackman sinc should be compared against a verified Annex 2 / EBU Tech 3341 true-peak reference with intersample worst cases and high-frequency attenuation. The implementation is *measured* and useful, but no claim of formal IEC/ITU certification or guaranteed 0.0 dB numerical agreement.
- Windows CTest covers 997 Hz −20 dBFS stereo reference, 12 kHz phase-shifted intersample overshoot, unsupported rate, silence, reset. Need CPU callback p95, 44.1/48/96 sample-rate hardware FAT and profile on an onboard GPU Windows PC before expanded production support.
- Master UI does **not** change meter LEDs, fader, Plugin Catalog, VST3 lifecycle, routing, Orbit, CI matrix, or #15 crackle closure.

References: [ITU-R BS.1770-5](https://www.itu.int/rec/R-REC-BS.1770-5-202311-I).
