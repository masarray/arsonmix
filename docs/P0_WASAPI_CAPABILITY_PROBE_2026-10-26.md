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
