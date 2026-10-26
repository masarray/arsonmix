# P0-11 — Truthful render callback cadence under variable JUCE buffer quanta

**Baseline:** `main@b2c3b06c66c800e938f537c591c7b392047373d4` (P0-10 ASIO CLSID registration guard).  
**Status:** P0 #15 0-FX crackling physical FAT remains OPEN; this is a measurement correctness milestone, **not** proof that output hardware underruns were removed.

## Verified external source contracts

- Pinned JUCE Windows WASAPI `getXRunCount()` exposes *input capture discontinuity flags* and may return -1 for unsupported paths; it is **not** comprehensive render underrun accounting. JUCE [AudioIODevice](https://docs.juce.com/master/classjuce_1_1AudioIODevice.html) documents that backend support and OS buffering vary. JUCE [AudioDeviceManager](https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html) may use callback-time estimates if native reporting is unsupported; do not present an estimate as native DAC starvation.
- Windows [IAudioClient::GetCurrentPadding](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding) provides the **instantaneous stream buffer occupancy**, not a cumulative underrun count. Reading a second IAudioClient obtained from MMDevice activation would not observe JUCE's running render client; do not add COM polling and assert it diagnoses the real render stream.
- JUCE [AudioIODeviceCallbackContext::hostTimeNs](https://docs.juce.com/master/structjuce_1_1AudioIODeviceCallbackContext.html) is optional; it must not silently replace the existing monotonic local callback-start clock, or mix incompatible origins across callbacks.

## Confirmed code bug and deterministic correction

`src/LiveAudioEngine.h` previously calculated `periodUs` from **current** `samples` and used it for BOTH callback CPU budget (correct) and START-to-START callback-gap expectation (wrong for variable blocks). If the prior callback emitted 480 frames at 48k and the new callback requests 64 frames after 10ms, the old detector compared 10ms to 1.333ms and falsely marked a >2.3x `callbackGapCount`. The inverse (previous 64 frames, current 480 frames after 4ms) hid a real scheduling delay.

New zero-allocation C++20 `CallbackCadenceEvidence` uses:
- `expectedGapUs = previousFrames * 1e6 / rateHz`, from last callback's quantum;
- `actualGapUs = currentMonotonicCallbackStart - previousMonotonicCallbackStart`;
- existing **heuristic-only** >1.5x `lateCallbackIntervals` and >2.3x `callbackGapCount` thresholds, preserving outward counter names/semantics;
- `periodUs = currentFrames * 1e6 / rateHz` stays unchanged for **current callback CPU-budget histogram**;
- rejects invalid frame sizes/rates and handles backward/non-increasing timestamps without unsigned wrap; fresh `audioDeviceAboutToStart` seeds an isolated timing generation;
- no query to Windows, driver, COM, heap allocation, background thread, mutex, FFT or IO in the audio callback. Exactly one render callback producer; state is only touched in JUCE lifecycle and render callback.

Status now exposes `callbackCadenceProvenance=HOST_MONOTONIC_CALLBACK_STARTS_PREVIOUS_BLOCK_PERIOD_NOT_RENDER_XRUN`, `callbackCadenceComparedIntervals`, `callbackCadenceClockRegressions`, `callbackCadenceLastExpectedUs`, and explicit 1.5x/2.3x threshold provenance. These and existing lifetime counters are **diagnostic estimates**, not native render underruns, automatic profile-optimization eligibility, latency guarantees, or driver clocks.

## Verification and limits

- New native `P0RenderCallbackVariableQuantumCadence` CTest checks 100,000 virtual callback starts with 64/128/256/480/512-frame quanta, zero false late flags at 48 kHz; the two inverted old-regression patterns; exact thresholds, clock reversal, invalid inputs and fresh 44.1/48/96 kHz device generations.
- Node contract checks previous-quantum arithmetic wired to production callback, source-labelled status/full report, and existing Windows CI build target. Windows MSVC Release build, all CTest and Chromium smoke must pass on exact PR head.
- No GUI files touched; no change to ASRC FIR/PI, hardware buffer selection, ASIO callback routing, or the VST3 graph. This does not solve existing unexplained 0-FX crackles or furnish actual driver-confirmed render XRUN provenance.

**Next P0:** provide a supported true-render provenance adapter ONLY if native JUCE client coupling can be proven, else mark `UNAVAILABLE`; connect zero-FX physical `SIGNAL CHECK` log to actual monitored hardware and loopback evidence. Long 60-minute drift/FIR and low-latency FAT remain separately gated. Never tune to 64-frame from a timing-only counter.
