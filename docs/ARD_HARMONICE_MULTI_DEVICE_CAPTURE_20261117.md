# ARD — Harmonice Multi-Device Capture Engine on JUCE 8 (Windows)

**Revision:** 2026-11-17 · **Status:** design contract, implementation NOT started / NOT hardware-qualified  
**Audit anchor:** `main@07cbb521fcbc173c7769001c89f321a88cd9db31`  
**Companions:** [PRD](PRD_HARMONICE_MULTI_DEVICE_CAPTURE_20261117.md) · [Research/evidence](RESEARCH_JUCE_MULTI_DEVICE_CAPTURE_20261117.md) · [Current architecture map](architecture/README.md)

> **Normative distinction:** exactly **ONE installed native mixer/DSP/VST3 graph and ONE render-clock-owner callback**. There may be **N separate capture-only callbacks**, each tied to an OS driver / its own clock. Prior 'no second callback' directives prohibit a second *render/mixing* engine, not the producer-only I/O callbacks that are fundamentally required for independently running devices. This exception is only active when the feature is explicitly enabled, reviewed and qualified; it does not authorize a second graph, duplicate main callback, or new WebAudio processor.

## A. Audit: actual current topology and reusable pieces

| Current code | Verified fact at anchor | Correct multi-input responsibility |
| --- | --- | --- |
| `src/LiveAudioEngine.h` | Has one primary `AsioPriorityDeviceManager deviceManager_`, one `JuceIndependentClockDevice independent_` (WASAPI capture/render pair), and `LiveSecondaryOutputDevice secondaryOutput_`. Has a single production render callback `audioDeviceIOCallbackWithContext` and callback ownership fences. | Keep current primary ASIO callback and real mixer as render authority. Add bounded capture-only endpoints without starting another rendering/FX path. No direct edit to SonKu/ImmerSIB DSP. |
| `engine/device/JuceIndependentClockDevice.h` | Uses two separately scheduled JUCE managers for one WASAPI capture and one WASAPI render; existing ASRC bridge and format fence. | Reuse conceptual split and hardened DSP bridge. Do NOT call its full render-forwarding adapter once per secondary device or create nested render callbacks. Extract/reuse narrowly-scoped capture pieces only after source/ownership audit. |
| `engine/device/AdaptiveClockBridge.h` | Preallocated SPSC ring + 48-tap/1024-phase band-limited polyphase ASRC; up to 16 channels/bridge, nominal 32768-frame capacity in existing path, bounded PI +/-2000 ppm. | ONE bridge per independent capture device clock, one writer and one primary render reader. Reuse ASRC only after quality/CPU/latency tests; do not multiply one bridge per mixer channel. |
| `engine/device/LiveSecondaryOutputDevice.h` | Optional secondary render-only WASAPI sink consumes real MAIN PCM through ASRC. | Distinct direction: multi-input must not consume OUTPUT1 as an input, alter its producer, or duplicate mixing; resource/lifecycle coordination needed. |
| `engine/device/JuceActivePortSnapshot.h`, `LiveInputPortRegistry.h` | Single-device input mask/physical-pin -> packed callback-ordinal map, generation-scoped. | Add device-qualified `SourceKey` and per-capture snapshot; **never** globally merge identical physical port numbers or assume callback ordinal = pin. |
| `engine/core/ChannelRoutingContract.h`, `PreparedNativeMixerSession.h` | Legacy compile path takes one enabled input-port list/mask; bound to one device epoch. `PreparedMixerBlock.h` is documented as device-independent/offline in older code. | Install a unified virtual input table on CONTROL, and pass one preallocated planar input view to the **actual installed** native ChannelId graph. Do not mistake offline test executor for the production graph. |
| `engine/core/NativeRenderAuthority.h`, `engine/core/DynamicNativeSessionPublisher.h` | Explicit render-owner and admission/retirement model, with current compatibility gates. | Respect active installed PCM authority and its exact graph/FX lease. No second render owner on device changes. |
| `src/NativeDeviceApplyCoordinator.h`, `src/NativeMixerSessionDocument.h` | Serialize device apply/readback and separate persistent logical mixer from physical hardware binding. | Extend physical binding transaction; output authorization stays a separate safety concern. No logical state reset on secondary capture changes. |
| `web/mixer-io.js`, `web/monitor.js` | Current device and route UI/ACK owners. | Add display and commands via existing bridge; no parallel JS routing system. |

Source inspection reconciles older design documents that said 'never another AudioDeviceManager': that rule remains valid for device setup itself and new render engines; capture-only device ownership is a new, explicit exception. It must not be generalized to other features.

## B. Evidence-grounded platform decision

1. JUCE **8.0.15** `juce_ASIO_windows.cpp` declares `maxNumASIODevices=16` and `currentASIODev[16]` and assigns distinct callback function templates to slots (source lines approximately 317–319, 1410–1467, 1525–1560). This establishes **internal slots**, NOT simultaneous operational support of all vendor drivers. It is not the GUI picker API or an SDK promise.
2. `juce::AudioDeviceManager` manages only one selected `AudioIODevice` per instance. Multiple instances may own different independently opened devices if vendor/OS behavior permits. Never pass the same ASIO hardware as two independent physical outputs and assume the driver is multi-client.
3. JUCE's ASIO `AudioIODeviceType::hasSeparateInputsAndOutputs() == false`. A secondary *ASIO device* still identifies one driver/device; **capture-only** is a requested channel mask / operation, NOT cross-selecting two ASIO drivers inside one JUCE device. **Driver probe is mandatory:** some drivers may reject zero playback channels or require internal duplex startup. Do not invent hidden hardware playback, turn on an unapproved output or claim success on a silent device.
4. Windows WASAPI may open distinct capture endpoints as independently clocked user-mode clients. Shared mode may compete for hardware resources with an ASIO driver exposing the same physical interface; explicit capability and conflict checks are still required. Exclusive mode is opt-in with separate user-facing resource warning, never an automatic workaround.
5. User-mode audio hosting requires no custom kernel/virtual driver. This does not mean executable reputation/SmartScreen or redistributable SDK licensing requirements disappear. JUCE 8 AGPLv3 and its pinned ASIO SDK GPLv3 terms require release-time license review.

Authoritative references: [JUCE pinned ASIO source](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/juce_ASIO_windows.cpp), [JUCE manager](https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html), [JUCE type](https://docs.juce.com/master/classjuce_1_1AudioIODeviceType.html), [Microsoft WASAPI](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi), [WASAPI exclusive](https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams).

## C. Authoritative topology (target, no code claim)

```text
 Native UI /preview/ + existing intent/ACK bridge
                | DEVICE_ADD_CAPTURE / REMOVE / REBIND, source patches
                v
 CONTROL: MultiCaptureDeviceCoordinator [serialized owner, NO DSP]
   +--> primary existing ASIO/WASAPI manager + verified OUTPUT 1/2
   |      | hardware INPUT A packed channel pointers
   |      +---------------> PRIMARY render callback [clock master]
   |
   +--> secondary ASIO B capture adapter (one callback, one clock)
   |      capture-only PCM -> SPSC FIFO -> ASRC B --+
   +--> optional secondary WASAPI C (future qualified)
          capture-only PCM -> SPSC FIFO -> ASRC C --+
                                                   |
                      PRIMARY render callback <-+-+
                        -- assemble fixed planar source view
                        -- ONE existing native graph (ChannelId/FX/WIRE/BUS)
                        -- MAIN -> existing primary ASIO OUT
                        -- optional OUTPUT1 consumes established MAIN PCM

 CONTROL owns driver open/close, format/identity attestation, graph compilation,
 epoch publication, session persistence and user-facing native ACK.
 CAPTURE callbacks are PRODUCERS ONLY, no VST3, GUI, PDC, source routing,
 no reading or mutating native mixer graph.
 PRIMARY render callback is the ONLY mix/DSP consumer/owner.
```

**No nested rendering**: do not wrap `LiveAudioEngine::audioDeviceIOCallbackWithContext` in several secondary callbacks. Only the master calls it once per primary output quantum. Source aggregation is a prepared input staging step within its permitted render transaction, before original channel DSP.

## D. Data identities, persistence, and compatibility

### D1. Source identity (control plane; proposed, not an existing ABI)

```cpp
enum class CaptureBackend : uint8_t { asio, wasapiShared };
struct CaptureDeviceKey {
    CaptureBackend backend;
    std::string registeredDriverKey;   // ASIO registry key + class identity;
                                       // driver CLASS is not a unique physical serial
    std::string endpointStableKey;     // WASAPI StableId when available
    std::string fallbackEndpointId;    // Windows endpoint ID, unique NOW not permanent
    std::string pinSignature;          // actual supported/active pin identity
};
struct PhysicalSourceKey {
    CaptureDeviceKey device;
    uint16_t physicalInputPin;  // pin index is scoped to device, not global
};
struct NamedSource {
    std::string sourceId;      // durable logical alias ID
    PhysicalSourceKey left;
    std::optional<PhysicalSourceKey> right; // mono if absent
};
```

The above is **semantic pseudocode**; serializers and native types must reuse the existing app's real source/session schema. Do not serialize registry CLSIDs or OS opaque identifiers to public telemetry. A hash of a driver registration class is *not* immutable device serial proof. When StableId is unavailable or not reliably mapped to the JUCE-opened device, remain in **identity-unverified / REMAP REQUIRED** rather than asserting safe unattended rebind.

Microsoft distinguishes `IMMDevice::GetId` (current endpoint ID) from the newer `PKEY_AudioEndpoint_StableId` (where supported); do not call the former permanently stable: [StableId](https://learn.microsoft.com/en-us/windows/win32/coreaudio/pkey-audioendpoint-stableid), [GetId](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevice-getid). For ASIO, preserve existing registry-attestation logic but do not mistake driver CLSID alone for unique physical hardware.

### D2. Two independent generations, not one global reset

- `DeviceGeneration`: the accepted primary output clock/device lifecycle generation, used for the production output safety fence.
- `CaptureGeneration[deviceKey]`: one per capture instance, invalidated on disconnect, re-open, pin-mask/rate/buffer change, or ambiguous identity.
- `GraphRevision`: existing logical/PCM graph publication revision. Changes only through the established compiler/publisher contract, never on every capture callback.
- `RequestId`: deduplicated UI/control transaction ID; no optimistic successful ACK.
- `SourceBindingRevision`: optional versioned physical input binding set, independent of logical ChannelId/EdgeId identity.

A secondary disconnect invalidates only the secondary source lease and its buffer; it **must not** invalidate the primary output epoch or purge FX/Show/WIRE. Physical output remapping follows existing trusted-output policy. New JSON fields are additive with a schema version and a deterministic legacy migration: existing single-device physical ports map to the current primary device-qualified key after verified native readback, not based on invented persisted names.

### D3. Prepared mixer input table

Use a CONTROL-compiled dense `UnifiedInputSlot` table:

```text
sourceId / PhysicalSourceKey -> deviceIndex, per-device portIndex,
                               callbackPackedIndex, compiledVirtualInputIndex,
                               captureGeneration, format, status
```

A render-time immutable table maps N input pointers to virtual planar scratch; existing channel selection consumes stable `compiledVirtualInputIndex`, not random callback numbering. Primary input pointers may be referenced directly when safe; secondary ASRC writes to preallocated scratch. All arrays bounded with explicit max-device/max-input/max-frames validation. **Do not dynamically rebuild or resize in callback.**

If a secondary disappears, affected virtual input spans are zero-filled with a bounded anti-click ramp while other sources remain active. Source meters must show actual capture PCM when SOURCE ONLY, but no accidental MAIN edge is installed. Reusing older `PreparedNativeMixerSession` helpers is acceptable only if they are on the proven production path; otherwise bind to the current `DynamicNativeSessionPublisher` / installed ChannelId graph.

## E. Module seams and thread/lifetime ownership

| Proposed owner | Lifetime / permitted work | Forbidden work |
| --- | --- | --- |
| `MultiCaptureDeviceCoordinator` (CONTROL) | one serialized transaction queue, inventory, driver identity, negotiated rates/masks, attach/detach, force-fail gating, versioned binding, ACK; idempotent request handling | no PCM processing, no synchronous COM/registry on audio thread, no independent mixer graph |
| `SecondaryCaptureAdapter` (one per independent device) | native JUCE device owner and callback; preallocated bounded capture ingress; format/error flags, no VST3 | callback cannot call mixer, allocate, mutex, log, plugin, WebView or device restart |
| `CaptureClockBridge` (per adapter, reusable `AdaptiveClockBridge`) | one SPSC writer (that adapter), one reader (primary render), ASRC ratio & occupancy; control-only preparation/reset when quiescent | combining multiple producers in one SPSC; resetting while callbacks are live |
| `UnifiedInputFrameAssembler` (primary callback only) | each render quantum pulls needed frames, stages virtual input spans (no heap), applies quiet transition, reports bounded counters | starting devices, editing source maps, VST3 object lifecycle |
| Existing native graph / VST3 | one render-side processing call on primary clock, existing immutable graph publication & plugin lease | any processing on secondary capture callbacks |
| Existing `NativeDeviceApplyCoordinator` / session | device output authorization, coordinate secondary teardown, preserve logical document and approved graph | duplicate manager that can change primary silently |
| Existing WebView2 audio I/O and source chooser | typed commands, pending/ACK/readback, presentation, compact groups | authoritative 'live' state from local JS or browser PCM |

Proposed files (names flexible; **do not make needless pass-through classes**): `engine/device/MultiCaptureDeviceCoordinator.h/.cpp`, `SecondaryCaptureAdapter.h/.cpp`, `UnifiedInputFrameAssembler.h`, `engine/core/MultiDeviceSourceBinding.h`. Integrate at existing command/graph seams only after MDI-1 proof. Avoid adding new abstract interface families before identifying a proven owner and a real test seam.

JUCE driver lifecycle and `scanForDevices` run only from the authorized serialized control path, with Windows COM apartment affinity respected where needed. ASIO callback thread lifetime and its shutdown must be fenced: disable producer publication, detach callback, stop/close JUCE device, await in-flight producer/consumer lease quiescence on CONTROL, then destroy SPSC ring/ASRC. A callback must never busy-spin waiting for control.

**ASIO secondary open safety:** first request input-only with zero output channels; read back *actual* masks, callback participation, and capture PCM. If driver requires duplex, investigate **only** a vendor-supported silent-output configuration with verified disarmed physical output and no unintended consumption of primary output. If no safe mode exists, mark unsupported; no invisible dummy audible output or automatic raw ASIO driver calls.

## F. ASRC policy and mathematical truth

The primary output clock defines one renderer timeline `R_m`. For each independent capture i, initial nominal source-to-render ratio is `R_i / R_m`. Long-term hardware drift requires adaptive correction. Correcting only sample-rate labels without occupancy feedback causes persistent FIFO drift even if both drivers report 48 kHz.

Per capture adapter (control-prepared): read actual device sample-rate and buffer size; compute capacity and target occupancy from measured driver period + jitter budget + FIR margin using existing `ClockBridgeSizing`; reject unsupported buffer combinations, rather than defaulting blindly to 24/50 ms. Do not allocate/reset FIR coefficients on RT. Use existing windowed-sinc polyphase interpolation and bounded occupancy correction (currently +/-2000 ppm in `AdaptiveClockBridge`); capture exact occupancy, priming, overflow, underflow, nonfinite samples and correction ppm. If correction saturates or priming repeatedly fails, degrade that input and surface a source-specific fault.

Avoid claims of shared clock merely because rates are both 48 kHz. Hardware word-clock/ADAT synchronization may support a validated common-clock optimization, but only with device-supplied evidence and tested driver behavior; no heuristic by device names. Always preserve the primary direct ASIO fast path with NO extra ring or SRC.

**Latency:** effective secondary input-to-output latency includes its capture period, ASRC reservoir, resampling group delay, mixer/FX plugin latency and render/output buffering. Report measured/driver-provided/estimated values distinctly. No blanket 'zero latency'; no promise that independent instruments are phase-aligned for multi-mic recording. For sensitive close-microphone applications, recommend one multichannel clock-synchronized interface until hardware delay alignment is measured and compensated.

**Realtime safety of staging:** render callback may deliver up to existing `maxFrames=8192`; larger blocks fail safely rather than overflow scratch. Per-device input/virtual slot limits must be explicitly negotiated before publish. The bridge `pull` is O(samples * channels * FIR taps) and must be budgeted for the intended two-device configuration; avoid one resampler per track. An inactive secondary costs at most a bounded branch/zero-fill in an explicitly prepared multi-input graph; when feature disabled the original callback fast path must remain untouched.

## G. Serialized transactions and ACK contract

Device add/remove never pretends to be a lightweight fader change.

```text
DISCOVERED
 -> PREFLIGHT (driver identity, OS endpoint, candidate channels, rate)
 -> MUTED_PREPARE (allocate bridge & scratch, pin primary config)
 -> OPENING (secondary JUCE instance, no graph/VST3 mutation)
 -> READBACK (actual device/name/masks/rate/period + callback evidence)
 -> PREPARE_SOURCE_MAP (compile immutable virtual ports and source binding)
 -> PUBLISH (one accepted render-boundary swap; capture generation)
 -> ACTIVE (subsequent matched native snapshot, not only 'open succeeded')

Any failure -> REJECTED / UNSUPPORTED / DEGRADED, secondary made silent,
                  old graph + primary playback preserved.
```

Proposed extension of *existing* command envelope (schema to be reconciled against current installed `arMixAction` and console-v8 contracts BEFORE code):

```json
{
  "requestId": "client:104",
  "operation": "ADD_CAPTURE_DEVICE",
  "expectedPrimaryDeviceEpoch": "7",
  "expectedGraphRevision": "48",
  "payload": {
    "backend": "ASIO",
    "requestedDriverKey": "driver-identity-from-native-inventory",
    "requestedInputMask": [0, 1],
    "ratePolicy": "AUTO_VERIFY_READBACK"
  }
}
```

Server ACK: `requestId`, `accepted/rejected`, `faultCode`, `acceptedCaptureGeneration`, `actualBackend`, `actualSampleRate`, `actualBufferSamples`, `actualInputPins`, `sourceBindingRevision`, `graphRevision`. GUI displays ACTIVE only after later matching **installed native readback**; operator patch intents preserve expected device+source generation. Use decimal-string IDs if reusing existing uint64 JSON ABI; the pseudocode strings are illustrative, not permission to define conflicting transport methods.

**Failure classes:** `ASIO_DRIVER_NOT_FOUND`, `ASIO_DUPLICATE_DRIVER_CLASS_UNVERIFIED`, `ASIO_CONCURRENT_OPEN_REJECTED`, `SECONDARY_CAPTURE_ONLY_UNSUPPORTED`, `PHYSICAL_OUTPUT_NOT_SAFE`, `WASAPI_ENDPOINT_AMBIGUOUS`, `SOURCE_PIN_SIGNATURE_CHANGED`, `NEGOTIATED_FORMAT_CHANGED`, `BRIDGE_LATENCY_UNSUPPORTED`, `BRIDGE_OVERRUN`, `BRIDGE_STARVATION`, `DRIFT_CORRECTION_SATURATED`, `PRIMARY_RENDER_MISSING`, `CAPTURE_DEVICE_DISCONNECTED`, `SOURCE_BINDING_STALE`, `GRAPH_PUBLISH_REJECTED`. All failures must carry actual affected source and recoverability; do not conflate bridge input underflow with proven Windows DAC underrun.

Recovery policy: secondary loss -> source muted, no entire session teardown; primary loss -> existing output fence/policy takes precedence. A reconnected secondary requires exact safe identity verification, fresh buffer and generation, and readback; do not replay FIFO from before unplug. User-approved previously verified output restoration follows established smart workflow; secondary addition never changes default output to a newly discovered one.

## H. Proof-first SPIKE design (not a product implementation)

**MDI-1 must run as an isolated Windows test executable using the exact JUCE 8.0.15 checkout, ASIO enabled at CMake and the same x64 toolchain as production.** Start with two *distinct registered vendor ASIO drivers*, not two handles to one ASIO4ALL adapter. Do not modify the production callback, GUI, VST3 or sessions in this spike.

Sequence: enumerate and attest both registered ASIO driver classes, choose explicit pin mask and backend, open A full-duplex as primary; capture monotonic callback evidence. Open B independently with input-only request; record **actual** input/output channel masks and negotiated rate/buffer; start B and count PCM frames, verify two callback clocks and concurrent continuity while A outputs a bounded test signal. Stop B, verify A continues, stop A and reverse order. Repeat with different buffer/rate candidates and unsupported driver behavior. If B refuses input-only, document exact error and cease unverified attempts; optionally test a vendor-supported silent-duplex profile under explicit safe-output policy. Test contention with identical ASIO vendor class separately as negative case.

The spike reports by source: requested/actual backend & device key (redacted externally), channel masks, rate, period, callback count and cadence, nonzero PCM count if a real signal was present, error codes, stop behavior and output safety. A 'success' from `AudioDeviceManager::initialise` without two live independent PCM streams is **not** acceptance.

**Go:** at least one physically tested *distinct-vendor dual-ASIO* hardware pair works concurrently without unexpected output, driver crash, unbounded glitches or primary reset; and a separately evaluated ASIO+WASAPI case. If the available test equipment cannot prove this, remain at RESEARCH/POC and do not market dual-ASIO support. **No-go:** any requirement to create a signed kernel driver; interleaved VST3 calls from capture callback; hidden second renderer; unverified dummy output; repeated open/close loops to fish for success.

## I. Deterministic CI and physical FAT

| Test class | Required stimuli / assertions |
| --- | --- |
| Source identities | Two devices both expose `Input 1`; exact qualified sources distinct; sparse masks 0,2,7 preserve actual L/R; same friendly name / changed driver class rejected |
| One producer per ring | B cannot write A's SPSC; stop/restart generation invalidates pending FIFO, no stale replay; no memory reclamation before both callback leases quiesce |
| Clock drift | Simulated +/-50, +/-300, +/-1000 ppm, 44.1->48, 48->44.1, 96->48, different producer/consumer block sizes and jitter/stalls; bounded fill, finite correction, truthful overrun/starvation |
| ASRC fidelity | DC, impulse, logarithmic sweep, white/pink noise and Nyquist-edge swept tones; objective frequency response, alias image levels, harmonic distortion and group delay versus direct reference; offline output checks before physical qualification |
| Callback realtime | Instrument/preallocate all buffers, no callback heap/lock/COM/scan/log, bounded runtime even when unplugged; measure actual callback-budget histogram across 0-FX and representative VST3 load |
| Primary parity | Same audio binary with secondary disabled: identical routing, latencies, fx lease, ASIO identity/outputs, no additional FIFO or slow callback path |
| PCM routing | A.Input1 -> CH01 and B.Input1 -> CH05, stereo B L/R to single stereo channel, SOURCE ONLY retains source meter but not MAIN; FX -> BUS -> MAIN exactly once; prevent graph double summation |
| Fault isolation | B open fails; B disappears; B rate/pin mask changes; app suspend/resume; ASIO driver reset; output-only disappearance; A remains audible when qualified, B source muted |
| Session/migration | Old .smix from single-device main loads exactly; source binding revision migration, save/restart/rebind; offline device references kept; no implicit source swap to different USB endpoint |
| GUI/transport | /preview/ native build, no duplicate source routers, pending+negative ACK until source-qualified installed readback; browser mock clearly marked not hardware evidence |
| Hardware FAT | Two vendor ASIO interfaces + mixed ASIO/WASAPI on actual Windows equipment, initial / reverse open order, 44.1/48 negotiated formats, 0 FX then multi-VST3, >=8h qualified soak, hotplug and sleep; channel-by-channel recorded evidence |

Suggested new tests: `tests/multi_capture_source_identity_test.cpp`, `tests/multi_capture_clock_bridge_test.cpp`, `tests/multi_capture_generation_lifecycle_test.cpp`, `tests/multi_capture_assembled_pcm_test.cpp`, `tests/multi_capture_native_transport_contract_test.mjs`, `tests/multi_capture_windows_dual_asio_probe.cpp` (explicit Windows hardware gate, not automatically 'green' in cloud CI), `docs/HARDWARE_FAT_MULTI_CAPTURE_20261117.md` (proposed).

Do not classify callback cadence gaps as proven DAC underruns. Preserve existing `backendXrunCount` and output starve provenance. Predeclare per-rig numeric CPU/latency/dropout and spectral acceptance thresholds before running physical tests; do not move criteria after observing a failing result. Existing 0-FX crackle FAT issue #15 remains a separate blocker where relevant.

## J. Release, rollback and implementation sequencing

- **MDI-0** publish research, PRD, ARD only; no new binary behavior.
- **MDI-1** independent native dual-driver probe, hardware evidence and pass/fail; no production audio changes.
- **MDI-2** secondary capture adapter + clock bridge + deterministic tests behind compile/runtime default-OFF feature gate; stop cleanly on failing identity/capacity.
- **MDI-3** primary render input assembler and accepted native graph integration only; stage source map atomically and preserve FX and MAIN routing.
- **MDI-4** native command/state/UX, session restore and safe hotplug lifecycle, build-exact WebView2 preview validation.
- **MDI-5** qualified hardware FAT + CI + packaging/legal audit; remove experimental label only after evidence.

Build release policy: if multi-capture is disabled or unsupported, the existing primary one-device path must remain byte-for-byte semantically equivalent (no new ASRC and no additional DSP owner). Feature flag rollback is the immediate safe revert, then revert isolated PRs as necessary. Each code milestone should merge only with its own tests and no change to stable SonKu/ImmerSIB sound or approved /preview/ appearance.

## K. Risk register and explicit design rejection

| Risk | Mitigation / decision |
| --- | --- |
| ASIO internal 16-slot claim misread as universal driver support | Explicit dual-vendor probe; slots are implementation capacity only |
| ASIO vendor refuses capture-only or concurrent access | Report unsupported / documented safe duplex mode; never blind driver retry or hidden audible output |
| SPSC corrupted by multiple capture producers | One SPSC per independent device; enforced ownership in tests |
| 'Both 48 kHz' mistaken for common word clock | ASRC on every independent clock domain unless physically proven shared |
| Automatic source remap to similarly named USB interface | Device+pin identity proof or REMAP REQUIRED; preserve session |
| Secondary callback executes VST3 | Hard boundary + callback contract test; one master graph |
| Device setup disrupts primary or secondary output1 | Serialized resource plan, keep existing fences, fail silent on affected device |
| Added ASRC accidentally slows direct ASIO | Feature-gated zero-additional-FIFO primary path, matched profiling |
| Source count changes mixer channel identities | Independent source patch table; preserve ChannelId/EdgeId and logical aliases |
| Docs conflict ('single callback' wording) | Explicit narrowly scoped exception in this ARD and architecture map only, never dual PCM engine |

## L. Sources and evidence provenance

Pinned JUCE 8.0.15: [ASIO backend](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/juce_ASIO_windows.cpp), [AudioDeviceManager implementation](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp), [ASIO license](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/asio/LICENSE.txt), [JUCE 8 license](https://github.com/juce-framework/JUCE/blob/8.0.15/LICENSE.md).

Microsoft: [WASAPI](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi), [capturing streams](https://learn.microsoft.com/en-us/windows/win32/coreaudio/capturing-a-stream), [exclusive mode](https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams), [IMMNotificationClient](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nn-mmdeviceapi-immnotificationclient), [StableId](https://learn.microsoft.com/en-us/windows/win32/coreaudio/pkey-audioendpoint-stableid), [IAudioClock](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition). Additional source-backed research and disputed claims: [research document](RESEARCH_JUCE_MULTI_DEVICE_CAPTURE_20261117.md).
