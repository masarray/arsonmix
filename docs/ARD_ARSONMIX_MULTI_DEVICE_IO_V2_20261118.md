# ARD v2 — One Native Render Graph, Many Physical Capture & Output Endpoints

**Revision:** 2026-11-18 · **Status:** proposed architectural decision; NOT a merged multi-I/O implementation.  
**Audited main:** 85d84195d06c8798ce3a4309b362766c13b07e14.  
**Related:** [Full PRD](PRD_ARSONMIX_MULTI_DEVICE_IO_20261118.md), [Research](RESEARCH_ARSONMIX_MULTI_IO_MVVM_20261118.md), [MVVM ADR](architecture/ADR_MODULAR_MVVM_FEASIBILITY_20261118.md), [Input-only #757](https://github.com/masarray/arsonmix/pull/757).

## A. Problem with naïve multi-I/O design

Two or more independently clocked audio devices do not share callback timing or true rate merely because sample-rate displays are equal. A generic 'add another AudioDeviceManager then concatenate pointers' approach will underflow/overflow, mismap identical pin ordinals and may call VST3 from multiple threads. A second callback cannot simply run the entire mixer: FX lease, session and graph ownership would race. Outputs on a second interface cannot be treated as extra pointers in the primary ASIO callback.

Current ArSonMix already has three distinct capabilities: PRIMARY ASIO direct renderer, WASAPI independent duplex mode, and optional WASAPI OUTPUT1 MAIN mirror. It also supports verified external output pairs on the PRIMARY device's physical pins. This architecture **extends these seams** rather than replacing JUCE, inventing an aggregate driver or using duplicate DSP renderers.

## B. Non-negotiable topology & clock ownership

    GUI approved /preview/
      -> one typed native request / ACK / later readback
      -> CONTROL: accepted .smix + device graph transactions
      -> CONTROL: prepared immutable SOURCE MAP + OUTPUT TAP MAP
      -> one accepted ChannelId/EdgeId PCM graph
          ^
          | A physical INPUT (primary ASIO, direct callback pointers)
          | B physical INPUT -> capture-only callback -> SPSC -> ASRC B
          | C physical INPUT -> capture-only callback -> SPSC -> ASRC C
          v
       PRIMARY output callback (one clock master, sole MIX/FX/VST3 renderer)
          | MAIN/MONITOR/BUS/CH post-processed taps, from ONE graph pass
          +-> primary ASIO physical output pins [direct, same clock]
          +-> independent B output queue -> ASRC -> B output-only callback
          +-> independent C output queue -> ASRC -> C output-only callback

**Terminology:** one PROCESSING callback authority ≠ one total OS callback. All capture/output devices can own I/O callbacks, but **only the PRIMARY render callback executes mixer DSP/VST3 and graph publication**. This is a narrowly scoped exception to old 'one audio callback' wording. Other threads are producer-only captures or consumer-only output sinks, never a new mixer/VST3 owner. Never use WebAudio for actual PCM.

**Master clock:** Primary active render hardware. If primary lost, disarm output, silence / stop installed physical playback and retain logical mixer document. Do not automatically select another loudspeaker/sink without exact previously verified authorization. An internal free-running timer is not an implicit automatic new master.

**Same-clock fast path:** Primary input/output physical pins are delivered directly to prepared graph. Primary output pairs (MAIN 1/2, BUS A 3/4, etc.) use already allocated output pointers. **No ASRC, FIFO, extra callback or added group delay** for this path. The secondary resources do not exist when option disabled.

## C. Device inventory and identity

An accepted physical endpoint is identified by backend + driver identity + best available OS endpoint evidence + readback rate/period/active channel masks and labels. Exact device key is scoped to a generation. For ASIO, current registry key/CLSID attest a registered driver CLASS, not a unique USB interface serial. For WASAPI, do not equate friendly name with a stable endpoint. Use PKEY_AudioEndpoint_StableId when available and verified against the actual active stream; otherwise use exact current IMMDevice ID with documented lifecycle limits. The current app's ASIO registry attestation and WASAPI endpoint guard are still controlling checks.

**Control-plane immutable accepted DeviceGraphPlan:**

- PrimaryDevice: backend, registered identity, actual capture/render rates/buffer, enabled input & output masks, verification generation and explicit output permission.
- AdditionalCaptureDevices: bounded device array, backend, vendor/endpoint proof, actual input mask/format and each captureGeneration; no global merge by bare pin number.
- AdditionalRenderDevices: bounded array, independently verified output mask/format, each sinkGeneration, allowed output routing and speaker safety state.
- SourceMap: stable saved alias ID -> DeviceKey + input pin(s) -> prepared virtual input slot(s) -> ChannelId. Source index is **not** ChannelId or callback ordinal.
- OutputTapMap: stable ChannelId/EdgeId/BUS/MAIN/Monitor tap identity -> DeviceKey + physical output pin pair, gain/tap point and output permission.
- Revisions: logicalDocumentRevision, primaryDeviceEpoch, independent capture/sink generations, sourceBindingRevision, outputBindingRevision, graphRevision, requestId.

Masks must be re-read from actual opened JUCE object and translated into the correct packed callback-pointer order. Two devices both exposing Input 1 or Output 1 are distinct namespaces. A saved desired binding remains valid as logical intent when device unavailable; it is never equivalent to an admitted physical route.

## D. Real device adapters: separate roles, no duplicate engine

| Adapter | Callback role | Buffer & clock | Safe lifetime |
| --- | --- | --- | --- |
| PrimaryJuiceAdapter (existing JUCE ASIO/WASAPI) | input+output + ONE native graph renderer | direct primary clock; zero additional resampler | existing LiveAudioEngine primary ownership and DirectRouteFence remain authoritative |
| SecondaryCaptureAdapter | writes source PCM; no FX, no render, no GUI or graph mutation | one preallocated SPSC per independently clocked capture; ASRC consumed by primary | CONTROL open/stop/readback; callbacks use generation leases |
| SecondaryRenderAdapter | consumes published pre-FX? **NO: only selected fully processed taps**, not VST3 | one preallocated SPSC per sink; ASRC maps primary to sink clock | stop sink, await producer/consumer quiescence on CONTROL, then reclaim queue |
| ClockDomainBridge | ASRC per distinct independent capture/sink | initial actual rate ratio plus bounded slow correction from occupancy; configurable measured reservoir target | prepare/reset only when both ends are stopped/quiesced |
| UnifiedInputAssembler | primary callback reads all verified captures, produces dense input planes for one graph | no dynamic allocation; muted/zero spans for unavailable source | preallocated scratch, immutable accepted lookup |
| OutputTapPublisher | primary callback fans out selected precomputed native stem/bus/MAIN signals | SPSC per independent physical sink; no processing graph second time | bounded producer writes; consumer backpressure never blocks master |
| MultiDeviceIoCoordinator | serialized control owner, endpoint inventory, transaction, resource conflicts, restart and session binding | no audio callback work; may use existing device manager coordinator where compatible | one owner for primary+secondary resource plan, explicit rollback |

The name PrimaryJuiceAdapter is only a role label here, **not a proposed class or product rename**. Implementation should retain existing LiveAudioEngine/JUCE primary code rather than introducing an adapter with no tested benefit.

**Input-only ASIO nuance:** JUCE 8.0.15 ASIO exposes 16 internal slots and hasSeparateInputsAndOutputs=false *within one ASIO device*. The candidate secondary driver must be opened as a separate ASIO object with only input channels requested and output pins proven inactive, and then tested for live PCM. If driver refuses input-only, only vendor-documented, physically silent and explicitly authorized duplex fallback may be evaluated; otherwise mark unsupported. Do not hide dummy audible playback or repeatedly open until lucky.

**Output-only ASIO nuance:** similarly verify zero input channels / inactive input. A vendor may require duplex. A secondary render adapter may use WASAPI with user consent where that is compatible; avoid opening the same hardware via both ASIO and WASAPI in a way that destabilizes primary.

## E. SRC, schedule and latency engineering

For each capture i: nominal frames consumed per PRIMARY render frame = actual capture rate_i / actual master rate. For each sink j: nominal PRIMARY frames consumed per sink frame = actual master rate / actual sink render rate. Independent oscillators require slow occupancy correction even at identical nominal 48 kHz.

Reuse the existing AdaptiveClockBridge FIR/polyphase engine **after tests**; its 48-tap interpolation and current +/-2000 ppm correction bounds are implementation facts, not a product-wide tuning target. Keep separate FIFOs / ASRC instances for each distinct producer->consumer SPSC pair. Never let N capture writers share one SPSC. Avoid CPU duplication by grouping all pins of one hardware clock into one bridge where practical (up to existing verified channel limits), not one FIR per mixer strip.

**Readiness is based on negotiated periods, actual FIFO capacity, clock error, and driver timing evidence**, not constant magic 24/50 ms for all devices. Use existing ClockBridgeSizing as a starting point, then benchmark. Validate at 44.1/48/96k, different block periods, positive/negative ppm drift, stalls, queue-wrap, sample sanitization, FIR spectral/alias response and CPU callback deadline. Implement bounded fade, never restore stale PCM after rearm. Publish target latency as measured, driver-reported or estimated separately; the actual acoustic endpoint delay is NOT given by host callback timing.

**Processing and PDC:** FX/VST3 runs once on master callback. Output tap is captured after the requested processing stage (e.g. post-FX/post-fader/post-PDC) by the already accepted graph. Each output sink receives that same signal identity with its own ASRC; it may have a different transport delay. Plugin PDC ensures internal graph correctness, **not** synchronized DAC phase across free-running USB devices. For phase-critical crossover / multiway loudspeakers, one synchronized 4/8-channel ASIO interface is the preferred and qualified recommendation. A nominal cross-device manual delay value cannot correct ongoing clock drift.

**Fault and overload:** sink B underrun affects B only; B's stalled callback must never block A. Capture B loss silences only channels bound to B while preserving Show and WIRE. Primary loss is a separate global output safety event. No callback alloc, mutex, waiting, filesystem, COM, registry, JUCE device scans, host restarts, plugin lifecycle or WebView. Output and capture callback memory must be retired via explicit producer/consumer quiescence on CONTROL, never freed with active lease.

## F. Native graph and routing integration

Actual current binding order: logical .smix session -> accepted ChannelId/EdgeId graph -> native DynamicNativeSessionPublisher -> LiveAudioEngine master callback -> verified physical output. Changes to source/sink mapping must be staged and compiled on CONTROL, then published atomically with real device generations. Missing secondary input means prepared zero source (anti-click) and optional live pre-WIRE meter state, not an invented PRIMARY pin or implicit MAIN route.

Primary ASIO output taps already exist via DynamicPreparedMixerBlock::setExternalOutput. The new OutputTapPublisher should reuse the signal taps *within that graph* for independent sinks; do not recompile a second DynamicPreparedMixerBlock or call processBlock per sink. Verify that output taps are not taken before FX/pan/mute/solo/PDC where a post-processing tap is requested, and prevent double-summing a signal already routed to MAIN on the same pin pair.

**Output type differentiation:** (1) DIRECT primary device pins, (2) MAIN_COPY optional OUTPUT1, (3) NEW independent selected BUS/CH sink. The current OUTPUT1 compatibility mode may be refactored to a generic stream sink only after proved output1 parity and unchanged safety semantics.

## G. Control transactions (target state machine)

    DISCOVER -> ATTEST DRIVER / ENDPOINT / PIN CAPABILITY
             -> RESOLVE SHARED HARDWARE RESOURCE CONFLICTS
             -> ALLOCATE SPSC / ASRC / OUTPUT SCRATCH (CONTROL)
             -> OPEN SECONDARY DEVICE (JUCE or justified native WASAPI)
             -> ACTUAL RATE / MASK / CALLBACK READBACK
             -> PREPARE IMMUTABLE DEVICE MAP + GRAPH CHANGE
             -> PUBLISH AT SAFE MASTER RENDER BOUNDARY
             -> ACK + MATCHING READBACK -> ACTIVE

At any failure: reject and clean up ONLY the attempted secondary, preserve old accepted graph, primary output permissions, native FX leases and .smix. Recovery is asynchronous CONTROL-owned and idempotent with requestId. Stale device/graph generation -> fail closed. Driver errors only enqueue a bounded status/atomic fault; CONTROL handles close/reopen.

**Example additive intent concept:** ADD_CAPTURE_DEVICE, ADD_RENDER_SINK, ASSIGN_SOURCE, ASSIGN_SINK_TAP, REMOVE_DEVICE. Do not create a second WebView transport; migrate/enrich the existing console-v8 requestId, deviceEpoch, baseGraphRevision, baseRosterRevision envelope and later accepted snapshot. A per-device generation is additional state, not replacement for primary epoch. Decimal uint64 stays a canonical JSON string.

## H. Hard boundaries with existing code and PRs

- Existing NativeDeviceApplyCoordinator is the only authorized primary device apply/rollback owner. A new MultiDeviceIoCoordinator manages additional children and resource conflict as an extension of that command serialization, not another dispatcher that silently mutates PRIMARY.
- Existing LiveSecondaryOutputDevice remains OUTPUT1 while new buses are proven. Existing independent WASAPI duplex path remains a distinct alternative, not a recursive wrapper around native render.
- Existing source/pin masks are **device scoped**; no 'max 256 global physical pin' assumption.
- M4 legacy route paths cannot become a second fallback renderer just because secondary device is present; only installed native ChannelId graph owns real sound.
- Session/Show and plugin binary state must never be destructively rebuilt by an I/O change.
- Keep approved /preview/ identical visually. UI changes are additive, typed and tested on the same actual rendered assembled app. No hidden duplicate UI view.

## I. Implementation milestones and no-go gates

| Milestone | Required deliverable | Evidence to proceed |
| --- | --- | --- |
| IO-0 | This PRD/ARD/research and maintenance ADR; CMake/JUCE source audit | No production DSP modification |
| IO-1 | Isolated Windows exact-JUCE multi-vendor ASIO probe + multi-output endpoint probe | Two physically distinct vendor ASIO instances produce independent verified PCM and safe playback; negative results kept as unsupported |
| IO-2 | Secondary capture adapter + ASRC + lifetime fixtures | B unplug, stalled clock, variable rate, close/reopen; A uninterrupted, allocation-free callback tests |
| IO-3 | Primary prepared unified input table + one-channel/bus graph | A1 CH01 and B1 CH05 produce separate live PCM, FX+BUS+MAIN only once; no flat port alias |
| IO-4 | Generic output tap identity + independent sink ASRC | MAIN->A 1/2; processed BUS->B 1/2 (not MAIN copy); B loss affects B only; PDC/no-double-FX |
| IO-5 | Existing GUI Source Picker + Output Picker, .smix binding migration, ACK/readback | No false LIVE/phantom pin, offline intent preserved, no extra user monitor ritual |
| IO-6 | Windows real hardware endurance/CI and release compliance | Real driver compatibility matrix, >=8h qualified soak, 0FX and representative VST3, 44.1/48, fault/recovery FAT, exact SHA binary |

**Release distinction:** INPUT FAT != OUTPUT FAT != JOINT FAT. No 'multiple ASIO supported' announcement before dual-vendor proof. No undocumented patch to JUCE native source or new kernel driver. No massive rewrite of real audio while Milestone D is still awaiting physical validation.

## J. Executable test map (proposed)

- Identity/source: same pin number from devices A/B stays unique, sparse pin masks & pin labels, ASIO driver class duplicate, WASAPI endpoint collision, no wrong speaker fallback.
- Native PCM: primary 8-output MAIN/BUS/CH verified direct pins; B capture combined with A; B selected independent BUS sink; one VST3 processBlock per master block; known test sine identity, no duplicate summing, SOURCE ONLY meter no MAIN until WIRE.
- Clock: long synthetic N capture/N sink producer/consumer jitter, 44.1/48/96, +/-300 and +/-1000 ppm, stalls, under/overflow, priming/correction saturation, spectral rejection and group delay.
- Lifetimes: unplug B, replug with different pin masks, sleep/resume, driver reset, primary output lost, secondary sink timer hang; no deadlock, no use-after-free, old queued PCM never leaks into new session.
- UI/ABI: requestId/epochs/revisions, stale/duplicate negative ACK, accepted vs installed vs audible readback distinction, .smix backward-compatible migration, approved WebView2 exact renderer and FX/GAIN/WIRE regression.
- Physical FAT: distinguish actual driver xrun, capture discontinuity, internal bridge underflow and render starvation where native backend exposes evidence. Never claim zero output dropout based only on JUCE callback-duration histogram.

## K. Technical references

[JUCE 8.0.15 ASIO](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/juce_ASIO_windows.cpp) · [JUCE AudioDeviceManager](https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html) · [JACK multiple devices](https://jackaudio.org/faq/multiple_devices) · [Apple clock/drift](https://support.apple.com/en-us/102171) · [Microsoft WASAPI](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi) · [PipeWire combine-stream](https://docs.pipewire.org/devel/page_module_combine_stream.html) · [VB Matrix](https://vb-audio.com/Matrix/) · [research decision record](RESEARCH_ARSONMIX_MULTI_IO_MVVM_20261118.md).
