# P0-02 — ASRC signal-integrity hardening (2026-10-26)

## Change
The prepared `AdaptiveClockBridge` previously allowed NaN targetLatencyMs to bypass numeric comparisons then convert to an integer FIFO target (undefined behavior). Reject nonfinite target values before conversion; cap preallocated ring capacity at 2^18 frames (~16 MiB at maximum 16 channels).

At the capture ingress only, replace nonfinite PCM with zero before storing in the FIFO. Otherwise a malformed virtual/driver float propagates through 48 FIR taps into the output and third-party VST3 DSP. Existing frame copy now includes one bounded finite classifier, plus an atomic count update **only on affected blocks**. No new thread, dynamic allocation, lock, timing query or OS access inside audio callbacks.

`bridgeNonFiniteCaptureSamples` reports a cumulative count of **sample values sanitized by the bridge**, not confirmed physical driver xruns. Valid signals are unchanged by the sanitizer.

## Deterministic CI gates
Extend existing Release-active `AdaptiveDualClockBridge` CTest to exercise:
- malformed NaN/Inf latency and ring-size rejection;
- capture NaN/Inf injection, all-finite render samples, exact sanitized-sample count;
- independent 48k capture -> 32k render, 1kHz passband energy and 19kHz input above render Nyquist alias rejection, with fixed virtual clocks and no sleep;
- existing 44.1k/48k, ±100/300ppm, stereo matching, long render-quantum headroom, FIFO under/overflow accounting.

Neither synthetic CI nor spectrally bounded FIR tests prove real hardware clean, true physical render xruns, low latency or third-party VST3 interoperability. Keep P0 #15 open until consolidated hardware FAT. No changes to GUI, user profiles, direct ASIO or routing behavior.
