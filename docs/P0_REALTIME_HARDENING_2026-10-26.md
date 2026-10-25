# P0 realtime hardening and device lifecycle contract — 2026-10-26

**Engineering snapshot:** GUI visual baseline remains locked; this patch removes a per-chunk audio-buffer resize from the realtime callback and adds a generation-safe fail-closed session lifecycle primitive.

## What is implemented
- Preallocated two-channel scratch memory remains sized at `maxFrames`. Each audio chunk uses a non-owning `juce::AudioBuffer<float>` view with exact chunk length for VST3 `processBlock` instead of invoking `scratch_.setSize(...)` within the callback. JUCE 8.0.15 `AudioBuffer` keeps a small inline channel-pointer array, so the 2-channel reference does not heap-allocate. This change reduces callback bookkeeping and protects the backing allocation.
- `SessionLifecycle` is a plain C++20 control-thread transition component with OFFLINE → PREPARING → PRIMING → RUNNING → DEGRADED → RECOVERING / FAULTED transitions, fail-closed output permission, bounded three recovery attempts, generation tokens, and a deterministic CTest.
- Existing production device routing is **still** JUCE joined callback. The lifecycle component is **not yet attached** to a custom backend. Do not describe the existence of a testable state machine as automatic recovery working on real Windows devices.

## Remaining production P0 work
1. Implement separately clocked native Windows capture and render streams (WASAPI/ASIO feature gate), render-driven schedule and measured backend-provenance starvation counters. Keep direct JUCE ASIO/same-clock path.
2. Integrate prepared SPSC FIR adaptive bridge only when independent clocks are measured; validate FIR passband/stopband/alias rejection, latency, 60-minute drift and discontinuity injection.
3. Connect `AudioProfilePolicy` and `SessionLifecycle` to backend transactions, cancellation, last-known-good rollback and device notification/hotplug.
4. Measure callback allocations / CPU p99, CPU underrun, power/device suspend, monitor fade and final Windows hardware FAT at 44.1/48 kHz with 0 FX and real third-party VST3. Keep issue #15 open until actual sound acceptance.

**User workflow:** no manual testing on every commit. Once integrated, SIGNAL CHECK → COPY FULL REPORT → paste into engineering chat, then final hardware acceptance.

**Proof standard:** Windows Release compile + all CTest, Chromium GUI lock and code review are required for merge. These are not substitutes for hardware audio acceptance.
