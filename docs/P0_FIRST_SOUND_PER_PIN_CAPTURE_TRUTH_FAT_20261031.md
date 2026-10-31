# P0 — Capture Truth: 64-port native PCM isolation and first-sound FAT

## Release scope
This milestone repairs **measurement truth** for the current production single JUCE callback: a valid direct hardware input must be measurable before any routing is applied, with OUTPUT safely muted. It does not add a second audio engine or perform signal synthesis/ASRC automatically.

### Native engine changes
- `CaptureBoundaryEvidence` now gathers the peak and signal-bearing block history for **up to 64 callback-ordered channels during the existing one-pass capture PCM scan**. All arrays fixed-capacity, preallocated; RT only performs bounded sample reads and lock-free atomics. This is evidence, not DSP repair; 0FX crackle issue #15 remains open until physical measurement.
- NORMAL's previous UNPATCHED early return set input peak to zero and skipped input callback telemetry, falsely making a driver appear silent. It now invokes the existing capture observer **once**, publishes raw input peak/per-port signal history, increments callback counters, then clears every output and exits. No VST, routing PCM, or audio bypass is invoked. MAIN remains silent.
- Existing accepted-route path also publishes the same epoch-bounded port observations without a duplicate scan. Device START and STOP reset all per-port data. Status returns `captureCallbackPeaks`, `captureCallbackSignalBlocks`, `captureObservationBlocks` and an explicit `JUCE_COMPACT_CALLBACK_ORDER_RAW_BEFORE_ROUTING_AND_OUTPUT_MUTE` provenance.
- The same existing one-click native Copy Diagnostic Report already serializes this engine status and its correlation metadata. No audio sample data, file, or driver state is captured from the callback.

### GUI operator changes
In **Audio I/O → DEVICE / DRIVER**, a single 10pt status line after START AUDIO shows:
- `RAW PCM NOW`: actual measured pin peak dBFS, with pin names only if the native device-generation and enabled pin mask both match the callback channel count.
- `RAW PCM SEEN SINCE OPEN`: a previous nonzero PCM block, distinguishing momentary silence from a never-fed device.
- `RAW INPUTS SILENT`: all enabled capture pins have remained below `0.0005` linear since last driver open. Check Maono Routing Center, physical MIC and VC sources.
- `DRIVER MASK/GENERATION CHANGED`: labeling unsafe; refuse to guess which ASIO/virtual pins are live until refresh. No phantom channels are created.

No blur animation, popup, new card, polling worker, round-trip JS audio, per-frame DOM repaint, or second DSP callback. Compact read-only text refreshed from existing native telemetry; WebView and standalone HTML are byte-identical.

## MAONO PS22 physical acceptance procedure
1. With headphones/amp **low**, close other apps that may hold exclusive Maono ASIO; MUTE OUTPUT and any armed secondary stream.
2. In ArSonMix choose `ASIO → Maono ProStudio 2x2 ASIO → APPLY DEVICE`. **CURRENT AUDIO DEVICE** must read ASIO and actual sample rate/buffer. External Audio/MIDI Settings of another app is NOT a native ArSonMix ACK.
3. Expand **BUFFER / SAMPLE RATE / HARDWARE PIN ENABLE**. For MIC test select real MIC input; for VC playback test enable the corresponding driver-reported `Maono VC In 3 + 4` pair and `Maono HP Out 1 + 2`. Apply if a port mask changes. No hard-coded index assumption.
4. With physical output **MUTED and no route**, feed a steady known source. New **RAW PCM NOW** must show exactly the actual active MIC or VC pins; silent pins remain silent. This proves callback PCM independent of the dry mixer routing. `NO CAPTURE CALLBACK` after active driver means investigate driver/callback lifecycle.
5. Patch measured physical ports to CH01/CH02, APPLY native routing until ACK, then START AUDIO with low level. Compare input raw signal, output MAIN levels, and audible output.
6. For crackles, repeat 0FX baseline at 44.1k/512 and 48k/256 (only when driver reports these), test direct ASIO vs Windows Audio/VB Cable **without auto-switching**, use SIGNAL CHECK → Copy Diagnostic Report. Compare capture per-pin signal and zero-head candidate counters vs callback cadence/over-budget counters. Never label silence or heuristic gaps a proven backend XRUN.

## Evidence and limitations
- C++ `tests/capture_boundary_evidence_test.cpp`: per-port peaks/history, null source, split active pin channels, non-finite PCM, empty input, device epoch reset, zero-head heuristic unchanged, 100k-block soak.
- JS I/O operator contract checks unpatched callback path, native readback, generation guard and embedded standalone parity.
- This is **hardware FAT candidate**, not hardware FAT PASS until an actual MAONO PS22 test returns per-port PCM and clear 0FX output. Do not invent audio readings.
- M3D dynamic Bus session already merged but not connected to the production 10CH+4AUX callback. M3E dynamic Bus/UI and VST lease/Show cutover remain separate milestones.
