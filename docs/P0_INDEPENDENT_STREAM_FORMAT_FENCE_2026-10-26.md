# P0-08 — Immutable negotiated stream fence for independent WASAPI

**Checkpoint:** `main@7333baa95ba58b55758cefa576279712d2447800`, after merged P0-07.
**Issue:** [P0 #15 — crackling / audio reliability](https://github.com/masarray/arsonmix/issues/15).
**Hardware FAT:** STILL UNQUALIFIED. Do not present this as proof of low-latency audio or physical zero xruns.

## Observed gap

The independent JUCE capture/render implementation prepares `AdaptiveClockBridge` once with *negotiated capture/render sample rates, driver periods, two FIFO channels, and selected JUCE channel masks*. Its capture/render `audioDeviceAboutToStart` previously checked only the changed sample-rate. A driver restart retaining its friendly name but changing its active channel map or buffer period could silently run with obsolete ASRC configuration. Capture's realtime callback also accepted a changed count of JUCE input channels, potentially changing routing/duplicating a source silently. A similar render channel-count substitution was unchecked.

## Architecture: one P0 fail-closed negotiated-format contract

- New pure, portable `IndependentStreamFormatFence`: a **prepared** capture/render route snapshot includes full JUCE name, negotiated sample rate, buffer period, active channel-bitmask string (not capped at 64 channels), and logical channel count. Its positive validation checks 8–384 kHz, bounded positive frames/channels and counts set bits in the channel mask. Full Windows MMDevice identity is separately verified by the existing two-phase guard; a name does not prove an endpoint GUID.
- At `audioDeviceAboutToStart` *only*, compare all snapshot fields to actual opened JUCE stream. This is a device lifecycle event, **not** per-block DSP. First mismatch is sticky (atomic compare/exchange), with explicit source-labelled cause: device name, rate, buffer quantum, full mask or channel count.
- On actual capture/render callbacks, only compare JUCE-provided channel count against the already prepared immutable count: **O(1) integer comparison + one atomic fault read**, with no string, heap, ASRC reallocation, COM, device query, delay, lock or log. If mismatched or already faulted, capture discards incoming data and render zero-fills output, avoiding an unqualified map or stale-ASRC output. The existing 12 Hz control thread sees the fault and uses pre-existing bounded DIRECT restore (no automatic retry of failed independent WASAPI).
- Sticky fault reason appears in native status `bridgeNegotiationFault` and is **prefixed** as `format=<reason>` in the last-recovery event, before truncated verbose JSON. `bridgeNegotiationProtection` explains exactly what is guarded.
- `resetStopped()` may run only after both JUCE callbacks have been detached. New session snapshots are independent of previous generation. The existing control-side route `DirectRestoreBackoff` generation controls still apply.

## Things this does NOT claim

- JUCE can produce **variable-sized** callbacks in a normally negotiated fixed nominal quantum; do NOT compare every callback's `samples` to nominal buffer frames. The exact negotiated buffer-period comparison belongs in **about-to-start**, not inside `audioDeviceIOCallbackWithContext`.
- Complete immutable endpoint ID binding throughout an ASIO session is not yet solved. WASAPI MMDevice identity is verified twice around opening, not bound to the underlying IAudioClient handle.
- Playback hardware underrun detection is still unavailable through the pinned JUCE API. Format mismatch and callback silence are not driver-confirmed render underruns.
- No ASIO DIRECT callback or direct timing path, FX graph, JUCE version, FIR DSP, VST3 lifecycle or user-approved GUI was changed. No "auto optimal 64 frames" experiment was introduced.

## Deterministic validation

- New `IndependentStreamNegotiatedFormatFence` native CTest covers 100,000 stable callback observations, capture/render mismatch, changed sample rate (48 → 44.1 kHz), nominal period, **same-count-but-different** full channel masks, name switch, sticky fault and reset, invalid format/mask and >64-bit bitmask.
- Existing `independent_clock_adapter_contract_test.mjs` checks actual integration (both JUCE lifecycle callbacks, both RT channel count fences, render zero-fill, status, recovery provenance and CMake/Windows CI gate). Full Windows MSVC Release + CTest + native Node + GUI Chromium required before merge.
- Subsequent work: an OS-backed stable ASIO identity/capability generation, real backend-source-labelled render starvation observations (or unsupported), an AUTO/STABLE coordinator with cold-start validation/hysteresis and rollback, long ASRC jitter/drift/spectral benchmarks, then **consolidated actual-device FAT**.

**Engineering decision:** unchanged physical latency in clean DIRECT/ASIO routing and no variable callback jitter false positives. Safety has priority over output continuing with a stale route.
