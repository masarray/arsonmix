# P0-05 — Direct ASIO route authorization fence

**Date:** 2026-10-26  
**Scope:** JUCE 8.0.15 direct ASIO/WASAPI *host* route protection, not a native driver reimplementation.  
**Status:** implemented control/RT guard; hardware FAT remains OPEN in [P0 issue #15](https://github.com/masarray/arsonmix/issues/15).

## Problem verified in pinned JUCE source

In JUCE 8.0.15, `AudioDeviceManager::audioDeviceListChanged()` checks whether its active device's reported name remains in the latest list. When absent, it invokes `initialiseFromXML(*e, true, ...)` and may retry default devices. This is desirable for many desktop apps but unsafe for live mixing: an unplugged ASIO interface should not silently reroute a live microphone to another output. ArSonMix PR #52 verified a saved ASIO route **only after startup**; subsequent device events had no output authorization fence.

Also, the callback was intentionally registered before `initialise()` so AUDIO SETUP remains usable after startup errors; this makes it essential that processing does not emit any audio before startup route verification is complete.

## Implementation

- `DirectRouteFence` is a small standard-C++20 object with atomic, latchable route authorization. The control plane fingerprints **backend type + JUCE-reported device name** only at `audioDeviceAboutToStart()`, successful validated startup, internal same-route restore, or explicit AUDIO SETUP confirmation. There is **no hashing in the sample loop**.
- At startup, DIRECT is **unauthorized** until a working input+output device has been opened and the saved backend/names verified. A failed saved-ASIO start keeps the callback registered but output remains gated until explicitly selecting a working device.
- Unexpected backend/name change at a later `audioDeviceAboutToStart` latches `UNEXPECTED_DIRECT_BACKEND_OR_DEVICE`. A later automatic reconnect—even to the original name—does not silently re-enable host audio.
- The direct realtime callback uses one cheap atomic permission check. If unauthorized it writes zero to all output channels, increments an atomic `directSuppressedFrames` counter, and returns **before** plugin DSP or mixer graph traversal. No allocation, lock, I/O, device restart, waiting, resampler or extra FIFO. This silence is **intentional host suppression**, not a claimed hardware xrun.
- Opening native AUDIO SETUP suspends direct output before the chooser becomes visible. Closing it explicitly approves the currently opened, full-duplex device (both channel directions active), or remains gated if no valid route exists. No JS/CSS/HTML changes.
- A known-good DIRECT restore from experimental independent WASAPI must reopen exactly the previous JUCE name/type before authorization is rearmed. Independent capture/render retain their separate native Windows endpoint identity check; DIRECT fence does not impose ASIO-only rules on the render-clock-master ASRC callback.
- Failed/unapproved selection is never persisted to `audio-devices.xml`; only a confirmed live selection can be saved.
- Native status/COPY FULL REPORT includes `directRouteAuthorized`, `directRouteFault`, `directSuppressedFrames`, and clearly labels route ID protection `PINNED_BACKEND_AND_DRIVER_NAME_NOT_HARDWARE_ID`.

## Limitations

JUCE ASIO driver names are *not physical stable hardware IDs*. This guard detects backend/name substitution (especially ASIO → WASAPI), not replacement hardware exposing the exact same name; a native ASIO identity adapter or driver-specific identity must be added separately. It does not certify actual ASIO latency, eliminate physical audio crackles, measure true DAC underruns, guarantee every device hotplug sequence, or change the user's intended manual device choice. Full physical 44.1/48 kHz zero-FX + VST3 FAT is still required.

## Proof

Deterministic `P0AsioLiveDirectRouteFence` CTest covers unapproved startup, approved same-route restarts, mismatched API/name, sticky fault, deliberate same-route recovery, explicit user authorization and malformed identities. Native Node integration asserts guard wiring, pre-DSP zero-fill path and selector approval, with MSVC Release/CTest and Chromium smoke required on exact PR head. Visual GUI untouched.
