# P0-07 — Capability-gated low-latency planning and truthful native device evidence

**Date:** 2026-10-26
**Base:** `main@e8e98b535e9aa98e19bda178563936e8e8d84951` after P0-06 ASIO DIRECT channel-map fence.
**Scope:** control-thread policy + explicitly requested diagnostics, with no automatic backend/buffer changes.
**FAT:** NOT QUALIFIED. [P0 crackling #15](https://github.com/masarray/arsonmix/issues/15) remains OPEN.

## Why this is a P0 safety/performance prerequisite

1. The pre-existing `AudioProfilePolicy` selected the *smallest advertised buffer* for `LOW_LATENCY`, even when no physical test had demonstrated stability. An advertised 64-frame buffer is not proof of clean audio or reliable hardware scheduler operation.
2. `AUTO_STABLE` had no explicit exclusion for WASAPI exclusive mode, which can interrupt other apps without consent.
3. Two endpoints with the same purported stable ID could be treated as sharing one physical clock despite a changed backend or driver capability fingerprint.
4. Independent WASAPI closes the normal `AudioDeviceManager` while two separate managers run. `COPY FULL REPORT` used only the closed normal manager and could print **No active device**, losing actual render-device capability and capture-latency observations.

## Changes

- `AudioProfilePolicy` now fails closed when `LOW_LATENCY` is requested without **device/driver-specific independently recorded hardware validation** on both endpoints. No fallback to an arbitrary small buffer. Manual explicitly selected frames remain supported *if* the backend advertises them.
- `AUTO_STABLE` cannot select WASAPI exclusive. Exclusive requires both `ADVANCED_MANUAL` and a separate explicit approval flag.
- A shared-clock decision requires matching stable endpoint ID, backend API, capability fingerprint, and positive hardware clock evidence. Stale identity or API mismatches cannot be silently converted to an ASRC route. The implemented independent adapter is currently limited to **different verified WASAPI shared endpoints**, not cross-driver ASIO or exclusive.
- The supported buffer period scan is **O(N), allocation-free and order-independent**: choose the smallest reported period at least 10 ms for provisional conservative guidance, or the largest if no reported period reaches 10 ms. There is no claim that 10 ms is universally reliable.
- Pure `PeriodCapabilityEvidence` analyzes the **actual running** render device rate/buffer and JUCE-reported candidates in one pass, exposing `minimumAdvertisedFrames`, `conservativeCandidateFrames`, observed callback period and whether the active period was reported in the candidate list.
- `SIGNAL CHECK → COPY FULL REPORT` now targets the **actual render manager** (and actual capture manager for input latency) while independent WASAPI BETA is running; ASIO/DIRECT still uses the normal full-duplex manager. Period evidence is generated **only on explicit user request**, never in the audio callback or 12 Hz status timer.

### Important limits

- This slice is **read-only in live application**. The pure planner is a future policy module, *not the active startup coordinator*. `validatedLowLatency` is a testable input whose future source must be a trusted keyed physical FAT record. No such certification database is claimed or populated here.
- JUCE reports supported buffer sizes, but WASAPI shared can choose a backend/OS engine period independently. The report labels all suggestions `NOT_FAT_QUALIFIED_DO_NOT_AUTO_APPLY`. A nominal callback period is **not** measured analog round-trip delay or verified renderer xrun.
- ASIO driver names remain reported labels; a robust permanent physical-driver identity and generation binding is still required before implementing automatic stable-profile persist/reuse.
- No changes to approved GUI, DSP, FIR/ASRC, graph routing, plugin lifecycle, channel count, real-time cost or driver configuration.

## Verification gates

- `CapabilityAudioProfilePolicy` native CTest: fail-closed unqualified low latency, explicit exclusive permission, stale driver/API generation, direct-vs-bridge proof, negative/out-of-range requests, deterministic period candidate selection, mismatched independent capture/render rates.
- `NativeDriverPeriodEvidence` native CTest: invalid/nonfinite rates, empty/unsorted/duplicate period lists, current-not-advertised, sub-10-ms-only buffers, 44.1/48-kHz computed times.
- `smart_audio_diagnostic_contract_test.mjs`: actual independent render/capture integration, one-click-only evidence and no runtime period mutation; Windows MSVC Release + all CTest + Chromium smoke required at exact PR head and main merge SHA.
- Hardware FAT remains [#15](https://github.com/masarray/arsonmix/issues/15), not automated away by green CI.

## Next substantive P0 milestones

1. Native stable identity for ASIO and a verified adapter capability snapshot (version, API, endpoint ID, negotiated clock/rate/period). Never use a friendly name or channel count as a GUID.
2. Proven driver-render starvation/underrun provenance, or explicit unsupported signal. Timing gaps and ASRC queue losses are not hardware DAC underruns.
3. Bounded, generation-safe AUTO/STABLE **coordinator** consuming these preconditions, with strictly validated profiles, dedicated cooldown, user-visible reasons, rollback to verified DIRECT, no audio-thread reconfiguration.
4. Long-duration deterministic ASRC stress/quality/load verification with explicit reference, then **one consolidated** supported-device 44.1/48-kHz dry-path hardware campaign before claiming low-latency reliability.
