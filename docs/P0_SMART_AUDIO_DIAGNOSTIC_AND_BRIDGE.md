# P0 Smart Audio — native diagnostic + prepared clock bridge milestone

**Date:** 2026-10-25  
**Scope:** P0-00 one-click field support, P0-01 clock-domain core/feasibility, P0-02 standalone polyphase SRC simulation  
**Status:** experimental infrastructure; **the JUCE audio capture/render adapter is still a joined callback. Clock bridge is not connected to live physical audio.**  
**Reference:** [canonical ARD](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), [blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md), [hardware issue #15](https://github.com/masarray/arsonmix/issues/15).

## Product-owner workflow

No repeated user hardware trials after each internal PR. Build a coherent, testable **P0 infrastructure batch** in CI. Ask for one final physical acceptance campaign once the independent-stream backend adapter, adaptive clock bridge and device-policy integration are complete. Internal Windows Release CI and scripted virtual clock simulation still run on *every code change*: skipping tests would turn performance work into guesswork.

**One-click diagnostic:** run exact CI EXE, open **SIGNAL CHECK**, click **COPY FULL REPORT**, paste into ChatGPT. The native clipboard report contains:
- UTC/local timestamp as ISO-8601, app build family, Windows version, CPU, memory;
- requested and active endpoint names, API type, sample rate, channels, buffer, device start/stop generation;
- driver-reported supported rates, buffer sizes and input/output latency hints;
- exact host callback counters, worst callback time, host callback deadline p95/p99 **bucket upper bounds**, VST3 timings, plugin counts, routing, DSP levels;
- JUCE backend xrun meaning and explicit **render-underrun unavailable**;
- last 48 message-thread diagnostic events including user MARK CRACKLE (bounded by 64-event in-memory ring), dropped/truncated counts and graph event timestamps.

The user does **not** need to open the log directory. The report is copied to the native Windows clipboard, not posted online. It can include device names and events; review before sharing. The full rotating diagnostic log remains available if a longer timeline is later needed. No audio PCM, microphone recording, passwords or serialized plugin state is included.

## Performance and truth rules

- `RealtimeHealthHistogram` has 13 monotonically accumulated RT-safe relaxed-atomic histogram buckets; UI report displays p95/p99 **ceiling percentages** from budget bucket distribution, not fabricated precise latency percentiles. This costs one bucket increment per callback and never blocks.
- `AdaptiveClockBridge` uses a single-producer/single-consumer power-of-two bounded FIFO, generation-neutral indices, drop-newest overflow accounting, 48-tap/1024-phase precomputed Blackman FIR table, a slow bounded PI controller with integral anti-windup and fixed-rate-per-block adjustment. No memory allocation, locks or I/O in `push`/`pull`.
- Source clock and playback clock are treated as independent in deterministic simulation; 44.1→48, 48→44.1 and positive/negative ±100/300 ppm are tested without wall-clock `sleep`. No CPU-thread-per-channel model.
- Default bridge `targetLatencyMs=50` is an **experimental bridge-only** starting point, NOT the intended universal monitoring latency or a calibrated AUTO/STABLE policy. Single-clock ASIO must take the direct zero-SRC route.
- Without a dedicated independent capture/render callback adapter, the existing JUCE joined callback does **not** expose the clock domains required to validate the hardware correction. The bridge is intentionally kept out of the production audio callback. Connecting it blindly would add latency without resolving clock drift.
- The algorithm's FIR response, group delay, alias rejection and CPU load at 44.1/48/96 kHz require automated objective reference benchmarks before production promotion. FIR parameters remain documented experimental values, not a claim of studio-qualified SRC.
- Driver output starvation remains `UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE` in current host status. A fast host callback and zero captured discontinuities do NOT certify clean output audio.

## Detailed next integration gate (P0-01 adapter)

Build `DeviceAdapter` native Windows feasibility under a feature gate:

1. Discover stable endpoint identity, sample mix formats, supported buffer periods, default device roles and clock-domain evidence. No Bluetooth detection by name alone.
2. Expose independently scheduled capture and render streams (or prove pinned JUCE can expose them); never insert clock bridge within a callback that has already coerced both endpoints to one timeline.
3. Render output is master. Capture pushes to preallocated SPSC FIFO; render pulls with prepared SRC. No COM calls, allocation, endpoint restart or plugin preparation inside process callbacks.
4. A real render-starvation metric requires backend-provenance evidence (WASAPI padding/flags, low-water capture underrun, ETW) rather than calling callback start jitter an xrun.
5. Select direct shared-clock path when truly verified, bridged path for genuinely independent clocks, with rollback/fail-closed device state machine and rate/buffer negotiation on control executor.
6. Expose bounded adaptation health stats and automatic fallback decisions through the same native clipboard report. No heavy telemetry on realtime path.
7. Pass Windows CTest fake clocks, timing, graph handoff and Chromium smoke. Only after all P0 integration phases perform one real hardware campaign, documenting observed source devices and audible results.

## Functional acceptance remaining

- Real mixed-clock WASAPI capture/render path: **NOT IMPLEMENTED**.
- AUTO/STABLE profile picker + rollback: **NOT IMPLEMENTED**.
- Hardware 30-minute 44.1/48 kHz acceptance: **NOT TESTED**.
- VST3 structural FX add/remove/reorder without callback detach: **NOT IMPLEMENTED**.
- Current diagnostic report + independent clock math and deterministic simulation: implementation in PR; success only after exact commit Windows CI confirms.

This deliberate feature gate avoids shipping an unqualified resampler as though it were an ultra-low-latency audio fix.
