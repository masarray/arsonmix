# Research and Decision Record — Multi-Device Audio Capture, JUCE / ASIO / WASAPI

**Reviewed:** 2026-11-17  
**Scope:** Windows user-mode audio host for ArSonMix mixer (Harmonice is a separate built-in harmony plugin), not a kernel audio driver. Full independent multi-output design and MVVM analysis: [draft PR #763](https://github.com/masarray/arsonmix/pull/763).  
**Source audit:** `masarray/arsonmix main@07cbb521fcbc173c7769001c89f321a88cd9db31` and upstream `juce-framework/JUCE tag 8.0.15` (source not only newer JUCE master).

## 1. Summary: what is fact, what remains a hypothesis

| Question | Evidence-backed answer | Confidence / limitation |
| --- | --- | --- |
| Can JUCE 8.0.15 construct multiple ASIO objects? | Yes, source has `maxNumASIODevices=16`, static pointer slots, callbacks per template index, and slot creation. | **Source-verified capacity only.** Not proof two vendor drivers run concurrently on actual user PC. |
| Does stock JUCE Audio Setup offer one-click aggregation? | No; normal selector configures one `AudioDeviceManager` active `AudioIODevice`. | Source/API verified; a custom coordinator is required. |
| Can different independent audio capture callbacks feed one mixer? | Architecturally yes via one FIFO per capture, primary render-clock pull and SRC; ArSonMix already implements a WASAPI split capture/render adapter. | **Feasible design**; multi-input real hardware not yet implemented/qualified. |
| Can all ASIO vendors do two drivers concurrently, with zero playback output on secondary? | **Unknown; not guaranteed.** JUCE's slot capacity does not establish driver reentrancy or input-only support. | Hard MDI-1 hardware proof gate; reject unsupported combinations. |
| Is a new signed Windows kernel driver needed for direct use of installed ASIO/WASAPI drivers? | No. A user-mode host can open installed drivers/endpoints; no custom WDM device is being installed. | Driver-signing constraints matter only if future product creates a kernel/virtual driver. EXE reputation/signing is a separate concern. |
| Are two devices at '48 kHz' synchronized? | No. Nominal sample-rate equality does not imply a common hardware clock. | ASRC/drift-compensated FIFO is needed unless physical sync is proven. |
| Can PortAudio solve it without engineering? | No automatic solution; its own docs say **its PortAudio/ASIO implementation** does not currently support multiple ASIO devices at once. | PortAudio fact is library implementation-specific, not a universal ASIO standard rule. |
| Does VB-Audio Matrix demonstrate practical multi-ASIO on Windows? | Vendor advertises three physical ASIO slots and multiple Windows Audio endpoints. | Independent product reference, **not** JUCE proof or a guarantee for every vendor pair. |
| Can .smix survive unplug/replug? | Logical document already persists separate from device binding. | Requires qualified multi-source identity + schema and offline migration tests, not merely a saved friendly name. |

## 2. Source audit (pinned, inspectable links)

### 2.1 JUCE 8.0.15 ASIO implementation

[Exact tagged source](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/juce_ASIO_windows.cpp):

- Source lines 317–319: 16 ASIO slot capacity, `currentASIODev` array. This is a fixed process-internal table, **not** user-facing maximum supported devices.
- Lines approximately 343–353: slot-owned ASIO object lifetime.
- Lines approximately 1410–1467: callback function templates indexed by slot, preventing one global callback function from ambiguously dispatching all ASIO devices.
- Lines approximately 1525–1561: `findFreeSlot` and device creation; `hasSeparateInputsAndOutputs() == false` is backend rule for **one ASIO device**.
- Must confirm the precise `ASIOAudioIODevice::open/start/stop` behavior (including zero output mask) against live drivers; registry enumeration and an open return value alone are insufficient.

[JUCE 8.0.15 DeviceManager implementation](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp): `initialise` (~381), `setCurrentAudioDeviceType` (~751), `setAudioDeviceSetup` (~811), and `closeAudioDevice` (~970). Type switch can invoke reopen; do not do so during audio callback or without transaction fence. The [AudioDeviceManager API](https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html) and [AudioIODeviceType API](https://docs.juce.com/master/classjuce_1_1AudioIODeviceType.html) support explicit device/type setup but are not an aggregate I/O coordinator.

**Correct conclusion:** Multiple JUCE manager instances may be a practical implementation tool. Do NOT infer they are a fully supported aggregator or circumvent the ASIO driver limitations; test their ownership and simultaneous initialization on the pinned JUCE and exact Windows driver versions.

### 2.2 ArSonMix currently has one production render owner

[LiveAudioEngine](https://github.com/masarray/arsonmix/blob/07cbb521fcbc173c7769001c89f321a88cd9db31/src/LiveAudioEngine.h) owns `AsioPriorityDeviceManager deviceManager_`, `JuceIndependentClockDevice independent_`, and `LiveSecondaryOutputDevice secondaryOutput_`. These are different pre-existing routes, not simultaneous dual-ASIO input adapters.

[Independent WASAPI clock adapter](https://github.com/masarray/arsonmix/blob/07cbb521fcbc173c7769001c89f321a88cd9db31/engine/device/JuceIndependentClockDevice.h) deliberately uses a separate capture manager and render manager, forwards mixer processing only from render callback, and uses ASRC. It is NOT equivalent to two simultaneous ASIO capture devices.

[AdaptiveClockBridge](https://github.com/masarray/arsonmix/blob/07cbb521fcbc173c7769001c89f321a88cd9db31/engine/device/AdaptiveClockBridge.h) has realtime push/pull, SPSC structure, 48-tap/1024-phase FIR polyphase interpolation, PI correction bounded to +/-2000 ppm, and source input finite checking. However, its startup/occupancy controller is unqualified for arbitrary N devices or all block sizes; additional quality tests remain necessary.

[Single-device port registry](https://github.com/masarray/arsonmix/blob/07cbb521fcbc173c7769001c89f321a88cd9db31/engine/device/LiveInputPortRegistry.h) and [bound session](https://github.com/masarray/arsonmix/blob/07cbb521fcbc173c7769001c89f321a88cd9db31/engine/core/PreparedNativeMixerSession.h) currently map a single active physical port namespace into packed callback indices. Merely expanding a port count will collide e.g. A.Input1 and B.Input1. Multi-device must qualify source by device and publish an immutable new virtual-slot mapping, not create phantom physical pins.

[Current architectural ownership](architecture/README.md) protects one native **mixing graph** and permanent logical .smix state. Reconcile any blanket 'single AudioDeviceCallback' statements narrowly: multiple producer-only capture callbacks are permitted; there is still exactly one render/FX processing owner.

## 3. External engineering precedents

### 3.1 VB-Audio Matrix

[VB-Audio Matrix official](https://vb-audio.com/Matrix/) / [vendor press note](https://vb-audio.com/archive/Blog/VBAudioPressRelease_Oct2023.htm) document support for **three physical ASIO devices** plus Windows Audio device streams. It is evidence that multi-ASIO hosts can exist on Windows, *not* evidence that JUCE or all ASIO drivers automatically work. Do not copy a proprietary audio driver, UI or source code.

### 3.2 macOS Aggregate Device (clock design precedent, not Windows implementation)

[Apple Aggregate Device](https://support.apple.com/en-us/102171) documents one clock source, word-clock synchronization and drift correction for other devices. The principle aligns with one master renderer and per-independent-source resampling. This is not a JUCE Windows API and does not imply Windows has a built-in equivalent.

### 3.3 ASIO4ALL (risks, not dependency)

[ASIO4ALL official Device Aggregation](https://asio4all.org/device-aggregation/) cautions that separate devices need synchronizing clock assumptions and that drift is a real concern. ASIO4ALL need not be installed by ArSonMix. It can itself be selected as a driver, but its internal sources are then *one ASIO driver's* reported pins, not automatically additional independent vendor ASIO capture adapters.

### 3.4 PortAudio alternative rejected as shortcut

[PortAudio API Overview](https://portaudio.com/docs/v19-doxydocs/api_overview.html) explicitly says PortAudio/ASIO currently does not support opening several ASIO devices simultaneously because of its chosen ASIO backend/SDK usage. Therefore replacing JUCE with PortAudio is not a proven solution and adds hosting/migration complexity. This fact does not override JUCE 8's separate ASIO slots or prove all multi-driver usage impossible.

### 3.5 WASAPI exact semantics

[Microsoft WASAPI overview](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi), [capture client](https://learn.microsoft.com/en-us/windows/win32/coreaudio/capturing-a-stream), [exclusive-mode behavior](https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams) establish user-mode endpoint streams and distinction of shared vs exclusive access. API success must be followed by actual readback and callback evidence. `IMMNotificationClient` [device state changes](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immnotificationclient-ondevicestatechanged) / [device removal](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immnotificationclient-ondeviceremoved) are event sources for control-only recovery, never device reopening on callback.

[Microsoft endpoint ID](https://learn.microsoft.com/en-us/windows/win32/coreaudio/endpoint-id-strings) uniquely identifies an endpoint at a point in time, but [PKEY_AudioEndpoint_StableId](https://learn.microsoft.com/en-us/windows/win32/coreaudio/pkey-audioendpoint-stableid) explicitly clarifies that ordinary `GetId` is **not** a general stable identity for all lifecycle events. Acquire stronger stable evidence when available; retain a fail-closed mapping when not.

[IAudioClock::GetPosition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition), [frequency](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getfrequency) can provide OS-specific timing evidence only when supplied for the exact active stream; they are not a reason to invent timestamps or cross-domain alignment from arbitrary JUCE `hostTimeNs` (optional [JUCE callback context](https://docs.juce.com/master/structjuce_1_1AudioIODeviceCallbackContext.html)).

### 3.6 Audio SRC design precedent

[Sound Open Firmware sample-rate conversion architecture](https://thesofproject.github.io/latest/developer_guides/firmware/src_asrc.html) discusses variable-ratio asynchronous conversion and Farrow approaches. This is alternative design evidence, **not a mandate to replace** the existing FIR/polyphase ArSonMix `AdaptiveClockBridge` without measured spectral/CPU/latency failure.

## 4. Option decision matrix

| Option | Benefits | Main risks | Decision |
| --- | --- | --- | --- |
| Single existing JUCE manager + native AudioDeviceSelectorComponent | Almost no code | Cannot directly expose N independent ASIO capture drivers in one active device | **Reject** for new feature; retain existing single-device mode |
| Separate JUCE-managed I/O instances + capture-only adapters + one output render owner | Reuses pinned JUCE ASIO/WASAPI and existing ASRC; no kernel driver; minimal platform churn | Driver input-only support, multi-instance contention, clock drift, lifecycle | **Preferred** with mandatory hardware proof |
| Manual raw ASIO SDK direct host bypassing JUCE device adapter | Maximum control | Separate driver layer, callback ABI and duplicated correctness/security maintenance | **Fallback only if JUCE spike proves a narrow hard blocker**, isolate adapter and justify with evidence |
| New custom kernel/virtual ASIO/WDM device | Could expose one aggregate device to every DAW | Driver signing, complex OS audio/kernel safety, maintenance | **Explicitly excluded** |
| ASIO4ALL / VB-Audio / external aggregator as required dependency | Faster user workaround | Product dependency, setup burden and clock/support variation | **Do not require**; optional documented workaround |
| Switch whole app to PortAudio/RtAudio | Reuses other I/O abstraction | No established multi-ASIO guarantee; massive JUCE/VST3 hosting regression risk | **Reject without measured proof** |
| Merge different device callback pointers without ASRC | Very small patch | Unbounded clock drift, FIFO corruption, audible glitches | **Forbidden** |

## 5. Legal/distribution separation

- Steinberg ASIO bundled license in [JUCE 8.0.15 `native/asio/LICENSE.txt`](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/asio/LICENSE.txt) offers a proprietary Steinberg ASIO license or GPLv3 option.
- [JUCE 8.0.15 LICENSE](https://github.com/juce-framework/JUCE/blob/8.0.15/LICENSE.md) states JUCE modules AGPLv3/commercial dual licensing. ArSonMix itself is AGPL-3.0-only.
- Open source project maintainers should document chosen license paths, notices and compliance for combined binaries (including any Steinberg SDK components). Do not rely on informal 'it's open source, so any license works'; have release licensing reviewed.
- Using existing installed audio drivers from a *user-mode host* does not create a new Windows kernel driver-signing requirement. Unsigned EXE trust/warnings are distinct from kernel driver attestation.

## 6. Unresolved technical questions (explicit proof tests)

1. Does pinned JUCE 8.0.15 open/start two **distinct vendor ASIO** instances in one process on a real test rig, including reverse open/close order?
2. Is secondary ASIO input-only configuration genuinely supported by each driver? If it insists on output streams, is there a safe, explicitly verified silent profile or must it be rejected?
3. Does secondary driver use its own callback cadence concurrently with active primary without xrun or driver cross-reset?
4. Which driver identity data can be used to associate a stored input source with a physical interface when ASIO exposes only driver-class registry identity? At what point must GUI require manual rebind?
5. What are real per-device capture/resampler latency, jitter, long-run occupancy and correction dynamics for tested 44.1/48/96 kHz profiles?
6. Can FX state and installed MAIN graph stay intact across B detach, while OUTPUT1 remains available and has a separate writer/consumer lifetime?
7. Does a particular ASIO+WASAPI combination try to reserve the same hardware endpoints? How is conflict communicated without surprise fallback?
8. Which limitations should be documented for multi-microphone phase-critical recording vs independently sourced karaoke/live instruments?

## 7. Decision checkpoints

**Accept MDI-1 only after real dual-ASIO capture proof**, in addition to tests without hardware. If no qualifying hardware is currently available, do not claim source-verified ASIO slots mean release-ready dual-ASIO. Do not modify stable production renderer to find out.

After MDI-1, implement an optional *secondary capture* behind a default-OFF feature flag using one FIFO/ASRC per independent input device and the single renderer. Do not alter native graph's logical identity or plugin signal. Ship only with audited AGPL/ASIO distribution, Windows CI and reproducible physical FAT.

**Companion implementation contract:** [ARD_HARMONICE_MULTI_DEVICE_CAPTURE_20261117.md](ARD_HARMONICE_MULTI_DEVICE_CAPTURE_20261117.md).  
**Operator requirements:** [PRD_HARMONICE_MULTI_DEVICE_CAPTURE_20261117.md](PRD_HARMONICE_MULTI_DEVICE_CAPTURE_20261117.md).
