# P0-12 — Five-second pre-MARK CRACKLE evidence, with truthful source provenance

**Baseline:** `main@3e8f860c70cf99f213977b07a848dc1b34de418d` (P0-11 and independent, previously accepted GUI work). **P0 #15 remains OPEN** until a physical dry 0-FX Windows test is clean.

## Confirmed field-analysis defect

The existing native `SIGNAL CHECK → MARK CRACKLE` captured a **single lifetime status snapshot**. A reported `backendXrunCount=19`, `bridgeUnderflowFrames=1000`, or `callbackGapCount=8` could refer to faults hours ago, or even a prior device generation. It did not identify whether any counter increased in the seconds **before** the user heard the crackle. No single callback-start gap, capture discontinuity, or ASRC starvation proves the physical DAC itself under-ran.

Source-backed boundaries:
- Pinned JUCE WASAPI's `getXRunCount()` represents **capture DATA_DISCONTINUITY flags** when supported, not render-driver-confirmed XRUNs. `AudioIODevice::getXRunCount()` is backend dependent.
- Microsoft [IAudioClient::GetCurrentPadding](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding) describes stream buffer occupancy at an instant, **not** cumulative render xruns. A newly activated IAudioClient is not JUCE's running render client; do not introduce an unrelated COM client and call it physical render provenance.
- The existing P0-11 callback cadence remains a host-timestamp scheduling heuristic, not hardware render starvation.

## One isolated bounded control-plane implementation

New portable C++20 `engine/diagnostics/CrackleEvidenceWindow.h` receives **existing JUCE message-thread 1Hz `AUDIO_HEALTH` status snapshots**; there are no extra polling timers, no audio callback writes, COM calls, heap allocations or locks added to DSP. The window is a fixed 8-slot array of lightweight snapshots. The exact mark time is a monotonic `steady_clock` observation, not a wall-clock estimate. It picks the **oldest eligible baseline within the preceding 5 seconds**, requires a recent health poll (≤1.6s), rejects history gaps (>2.1s) and any route change, reconnect or device generation change. Scope includes capture/output device names, backend class, rate, buffer size, diagnostic mode, endpoint-ID correlation labels, and output/FX mute/bypass gates.

On user `MARK CRACKLE`:
- Preserve the existing `AUDIO_GLITCH_MARK` snapshot.
- Append a concise `AUDIO_GLITCH_WINDOW` *important event* in the diagnostic recorder, listing the window's duration, provenance, and **per-counter deltas** for host callbacks/over-budget/1.5x/2.3x cadence, WASAPI capture discontinuity, non-WASAPI backend-defined xrun count, ASRC capture underflow and priming silence, overflow, sanitized nonfinite samples, and actual independent capture/render callback counts.
- Treat unavailable backend counters, missing ASRC route, nonintegral/negative/overflowed data, and any observed counter reset as **N/A**, not zero. A true zero is emitted only when both values exist and remain monotonic in the same device/route scope.
- Always include `RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE`. Host callback scheduling anomalies and ASRC input starvation are correlated evidence only—**not proof of DAC or Bluetooth failure**. Window reason distinguishes insufficient history, changed route/generation, delayed snapshot, clock regression or no active device.
- The diagnostic logger is an existing control-thread → background file writer queue; neither the baseline observation nor mark touches the realtime audio callback.
- Keep approved GUI unchanged: same button, no new visuals, no blur, no sample/audio capture, no device reopen, no buffer tuning. The native report includes important events even after long 1Hz health logs; the concise window appears as a **separate significant marker** and does not depend on the long raw status fitting within the logger's 950-character recent-event limit.

## Verification

- Native `P0PreMarkerFiveSecondEvidenceWindow` CTest exercises correct 4–5s deltas, explicit N/A vs 0, source-specific capture flags, changed JUCE device generation, changed backend/route, stale 1Hz polls, disconnected device, invalid/counter-reset arithmetic, baseline overflow/ring wrap, elapsed time and required history.
- Node source contract verifies all production wiring, the fixed timer cadence, source labels, existing marker preservation and CI target. Gate is added to the existing MSVC Release job (**no new workflow**).
- Require exact-head Windows Release + all CTest, native Node and Chromium green; resolve PR review concerns and compare latest main immediately before merge.

## Field use and honest next step

Once a native release artifact is available, use **SIGNAL CHECK → OUTPUT_ONLY_TONE**, then **CAPTURE_ONLY**, then **DRY_THROUGH** at low headphone volume. Leave each mode active long enough for at least two 1Hz samples (prefer >5 seconds), click `MARK CRACKLE` immediately on the audible issue, then `COPY FULL REPORT`. A clean pre-marker window with no observed input/bridge/callback anomalies **does not mean no hardware render underrun occurred**, because the current pinned JUCE abstraction lacks reliable render-hardware XRUN provenance. A 5-second window can include unrelated events; temporal correlation is not a causal diagnosis.

Do not enable universal ASIO low-latency AUTO, guess at 64-frame buffers or close P0 #15 on this evidence alone. Next: physical dry zero-FX FAT with exact hardware, driver and routing; characterize native render backend opportunities *only if the active client/stream can be attached unambiguously*, then capability-gated profile policy with rollback.
