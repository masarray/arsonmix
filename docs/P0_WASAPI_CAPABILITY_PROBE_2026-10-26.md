# P0-01 — Windows Core Audio capability probe

**Date:** 2026-10-26  
**Status:** native read-only discovery implementation, NOT an active independent-stream backend.  
**Main architecture:** [ARD](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md) · [P0 implementation blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md) · [issue #15](https://github.com/masarray/arsonmix/issues/15).

## Native implementation

`engine/device/WasapiCapabilityProbe.h` queries **actual Windows Core Audio APIs** from the JUCE message thread when the user presses **SIGNAL CHECK → COPY FULL REPORT**. It never runs on the realtime callback and does not open/start/render/capture audio:

- `IMMDeviceEnumerator` enumerates up to 32 active endpoints per capture/render flow, with friendly names and a local noncryptographic hash of endpoint ID (the underlying Windows device GUID is not copied).
- `IAudioClient::GetMixFormat` reports the endpoint's shared-mode mix format.
- `IAudioClient3::GetSharedModeEnginePeriod` reports **default, fundamental, min and max** period frames **for the mix format**, if the interface is supported.
- `IAudioClient::GetDevicePeriod` reports default/min period in **100ns ticks**. It is distinct from DSP callback buffer length.
- Native results are separate from JUCE's active device report. Enumeration may include devices not currently selected in JUCE; no automatic name matching/clock equivalence inference.
- COM apartment is respected; errors are surfaced as HRESULT instead of pretending a capability exists; no raw PCM samples or Windows endpoint IDs are copied.
- Headless Windows CI runs a native COM smoke test that must not crash if no audio endpoint exists.

## Interpretation

The reported period is a Windows **capability**, not a measurement of actual Bluetooth latency, independent capture clock drift, end-to-end latency, physical render underruns or clean audio. `IAudioClient3` support varies with device and OS. A friendly name is not proof of common timing hardware. These values will inform capability-aware selection in the future adapter without guessed fixed buffers.

## Production gates still open

1. Capture/render endpoint binding by **stable Windows endpoint ID** with explicit default-role policy; robust reconnection notification and generation tokens.
2. Independently scheduled event-driven WASAPI capture and render streams; deterministic render-clock-master scheduling, MMCSS, COM lifetime.
3. Wire the existing prepared `AdaptiveClockBridge` for independently clocked sources only; maintain direct zero-added-FIFO ASIO/same-clock fast path.
4. Validate sustained ASRC spectral fidelity, bounded latency, under/overflow/driver ETW provenance and fail-closed automatic fallback.
5. Final real 44.1/48 kHz, 0-VST3 and third-party VST3 hardware FAT; hardware acceptance is not replaced by synthetic CI.

**User tests are not requested per commit.** The one-click report and integrated P0 hardware campaign remain the intended acceptance workflow.

## P0-03 safety slice — unique Windows endpoint ID verification (2026-10-26)

The opt-in, experimental JUCE independent adapter previously selected endpoints **only by friendly name**. Pinned JUCE 8.0.15 scans `PKEY_Device_FriendlyName`, stores its native `IMMDevice::GetId`, and resolves `createDevice` via a name-index lookup; duplicate names can receive JUCE suffixes and silently use first-match semantics. A name is therefore not a stable identity.

`EndpointIdentityPolicy` provides deterministic pure-C++ unique capture/render selection, refusing identical friendly names in the **same role** (identical names across capture and render roles are allowed), absent IDs and swapped/replaced stable IDs. `WasapiEndpointIdentityGuard` enumerates active MMDevice endpoints read-only with bounded maximum (128 per role) on the JUCE message thread, never in audio callbacks.

Transition sequence:
1. **Before DIRECT detach/close**, resolve both selected JUCE device names to exactly one active native endpoint ID per flow; reject ambiguity/missing/hotplug without interrupting DIRECT.
2. Open independent JUCE managers with names and channel masks unchanged; reject any JUCE setup substitution.
3. **Before attaching capture or render callbacks**, rescan native IDs and verify both match the original snapshot exactly. Reject and trigger the existing DIRECT rollback if replaced between discovery and open.
4. Status supplies only non-cryptographic 64-bit `bridgeCaptureEndpointIdHash`, `bridgeRenderEndpointIdHash` and transparent `bridgeEndpointIdentityVerification=UNIQUE_WINDOWS_ID_PREFLIGHT_AND_POSTOPEN_NOT_ID_BOUND`. Raw device IDs are not copied in field reports.
5. New deterministic policy tests and headless Windows COM smoke test join MSVC Release/CTest. No changes to DSP, ASRC latency, callback load, GUI assets or ASIO direct path.

**Limits:** This is an identity *guard*, not an ID-bound native WASAPI stream. Pinned JUCE still binds by name internally; there is an irreducible small race between second native enumeration and callback attach until a dedicated native ID-bound device adapter is built. No runtime MMDevice notification subscription or user-facing AUTO/STABLE planner exists yet. Genuine hardware shared-clock verification, true render underrun observability and physical sound FAT remain open. It is deliberately safer to refuse an ambiguous endpoint than guess which microphone or speaker should receive live audio.
