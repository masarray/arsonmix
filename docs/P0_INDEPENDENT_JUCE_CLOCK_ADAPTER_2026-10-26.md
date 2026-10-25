# P0 — Independent Windows Audio clock adapter (experimental live path)

**Date:** 2026-10-26  
**Implementation:** `engine/device/JuceIndependentClockDevice.h`, `src/LiveAudioEngine.h`, native `web/monitor.js`  
**Status:** Integrated live-path code, validated only by source contracts + Windows build/CTest until hardware FAT. **Not a production-ready guarantee or validated crackling fix.**

## Why this architecture is different from the old code

The old JUCE full-duplex device joined input and output into **one callback**, possibly hiding capture-reservoir delays and treating two independent physical clocks as one timeline. This was reported cracking with 0 VST3. This route uses **two different JUCE AudioDeviceManager instances**, not two fake callbacks sliced from the old device:

- A capture-only WASAPI-shared manager opens just the user-selected input endpoint and writes frames to a **single-producer, single-consumer FIFO**.
- A render-only WASAPI-shared manager opens the selected output endpoint and is the **master clock**. Its own independent callback reads from the FIFO through the already implemented polyphase FIR adaptive SRC, then calls the **existing** JUCE VST3 mixer callback with render-period samples.
- Endpoint initialization, temporary objects, FIR coefficient preparation, fallback and close run on the control thread. Capture and render callback paths have **no file I/O, logs, UI operations, device setup calls, mutex locks or allocated input staging**.
- Both devices can negotiate their real sample rates. Ratio uses captureRate/renderRate and bounded PI fill correction. **FIR bridge target is 24 ms**, an explicit *added* latency tradeoff for separately clocked devices. This is NOT ultra-low latency on all hardware; native ASIO direct path is preserved without the added bridge.
- Driver errors or rate renegotiation set atomic fault flags. A 12-Hz control timer tears down the independent path and attempts restoration of the exact prior JUCE setup; on restoration failure it mutes the output and records diagnostic evidence.
- One-sided P0-00 diagnostic tests and AUDIO SETUP revert to DIRECT first. Hardware selections for temporary dual managers are not written to the normal settings XML.
- VST3 add/remove/move and preset save/load are **blocked** while the independent renderer is active because legacy structural mutations detach the original JUCE callback. RT-safe bypass/parameter controls continue working.
- The native SIGNAL CHECK has **SMART CLOCK: DIRECT / INDEPENDENT (BETA)** opt-in. It reports FIFO fill, missing render frames, capture overflow and estimated correction ppm. **COPY FULL REPORT** includes all metrics.
- Backend capture `getXRunCount()` is reported with WASAPI capture-only meaning, not rebranded as a physical output underrun. FIFO missing render frames are precisely measured by the host bridge, but **actual render-engine output starvation remains unobservable in generic JUCE**.

## Guarantees and limits

This is a bounded, render-clock-master architecture with a direct fast path and rollback logic, rather than an unconditional SRC in the old joined callback. It does not assert all devices or Bluetooth profiles support a specified monitoring delay, and it does not make real audio-crackle test acceptance unnecessary. The user does **not** need to test every PR. Hardware acceptance is reserved for completion of P0 integration.

## Remaining P0 hardening before qualification

1. Audio continuity/alias-rejection quality suite and long-duration 44.1/48 kHz with >60 simulated minutes of source drift/jitter, including device stalls and queue-wrap.
2. Device-generation/notification handling, cooldown/hysteresis, last-known-good policy and bounded AUTO/STABLE fallback candidates using actual `AudioProfilePolicy` and `SessionLifecycle`, not merely a manual BETA switch.
3. True render starvation provenance via Windows native ETW or a version-pinned WASAPI adapter, separate from internal FIFO underrun, plus callback allocation and p99 CPU benchmark.
4. Proper device capability matching via stable endpoint identity, not friendly-name heuristics; source and output device open must reject mismatch in modes supported by the current JUCE backend.
5. Final real-device 0-FX and VST3 endurance FAT (44.1/48 kHz, changing USB/virtual/BT devices, suspend/resume), with log-copy evidence. Do not close P0 #15 until this passes.

**Approved GUI:** mixer remains unchanged. Only the existing native SIGNAL CHECK overlay is extended. Historical HTML standalone remains an offline UI preview and does not pretend to provide native independent clock audio.
