# PRD — ArSonMix Universal Multi-Device Audio I/O (Windows)

**Date:** 2026-11-18 · **Status:** proposed, not implemented or qualified  
**Audited baseline:** main 85d84195d06c8798ce3a4309b362766c13b07e14  
**Product:** ArSonMix mixer; Harmonice is a built-in harmony plugin.  
**Companions:** [ARD](ARD_ARSONMIX_MULTI_DEVICE_IO_V2_20261118.md), [research](RESEARCH_ARSONMIX_MULTI_IO_MVVM_20261118.md), [MVVM feasibility ADR](architecture/ADR_MODULAR_MVVM_FEASIBILITY_20261118.md), [input-only draft PR #757](https://github.com/masarray/arsonmix/pull/757), [physical external output contract](architecture/FEATURE_HOTKEY_FL_OUTPUT_EXECUTION_20261111.md).

## Product goal

A professional realtime Windows digital mixer accepts independent physical capture inputs from two or more compatible soundcards AND routes selected mixer signals to independent physical outputs, without an external aggregate driver or manual secondary mixer. Maintain low-latency direct ASIO path for the primary hardware and graceful rejection of incompatible vendor drivers.

Do **not** market arbitrary universal multi-ASIO support; it is hardware/driver-qualified. Do **not** conflate routing to 8 pins of ONE ASIO card with routing to two physically independent output cards.

## Four distinct feature tiers

| Tier | Operator capability | Clock implications | Actual current source state |
| --- | --- | --- | --- |
| T0: primary multichannel | Inputs 1..N to channels, MAIN 1/2, BUS A 3/4, BUS B 5/6 of **same ASIO interface** | Same driver/clock domain; direct output pin rendering; no new ASRC | Primary device and native external per-channel output code exist; hardware FAT must independently qualify claimed pin count |
| T1: additional capture | ASIO A Input1 -> CH01, ASIO B Input1 -> CH05 simultaneously | Each independent B capture needs FIFO/ASRC to A clock | **Not implemented**; PR #757 is research/design only |
| T2: additional sink | A outputs MAIN; B outputs **selected independent BUS or CH stem**, with actual own clock | SPSC/ASRC between master graph timeline and B render callback | Existing OUTPUT1 is a **WASAPI MAIN duplicate**, not arbitrary independent BUS routing |
| T3: integrated many-to-many | Several capture devices and several playback devices, with source/sink per-channel maps | One master render graph; one clock bridge per independent capture or sink and output delay evidence | Future qualified target; do not promise N universal |

Initial product qualification target: preserve T0 and OUTPUT1; prove one extra capture (T1) and one independently routed secondary sink (T2). Only then combine (T3). Initial T1/T2 should handle mono/stereo where driver supports it. N endpoints and 8-output speaker crossovers are scalable architecture goals, not first-version blanket promises.

## Operator journeys

| ID | Scenario | Observable acceptance |
| --- | --- | --- |
| IO01 | User has one 8-output ASIO card | MAIN 1/2, BUS A 3/4, BUS B 5/6, BUS C 7/8 from real physically present pins; no extra virtual device, no extra SRC |
| IO02 | Add second ASIO capture device | A Input1 and B Input1 appear as **distinct** sources, both deliver PCM simultaneously to separate channel meters |
| IO03 | Add B WASAPI capture while A ASIO plays | Capture/format/clock drift verified, B input can use native FX and WIRE while A continues |
| IO04 | Add independent output B | Destination dropdown selects MAIN, MONITOR, a verified BUS or stem for B; B receives that tap, not always copied MAIN |
| IO05 | SOURCE ONLY | Input meter remains live, MAIN receives nothing unless WIRE routes it, while specifically assigned post-FX external stem remains valid if operator requests it |
| IO06 | Parallel internal WIRE + external physical output | Both supported intentionally, with duplicate-to-same-physical-pin warning/collision guard; never silently detach WIRE |
| IO07 | B input/output disconnects | Only source/sink tied to B becomes unavailable and silent with fade; A, accepted graph and VST3 leases continue |
| IO08 | Device B returns with different ID/pins | Verify driver, endpoint identity and pin signature; restore only if safely matched, else REMAP REQUIRED; never substitute matching friendly name alone |
| IO09 | Save Show/session with B absent | .smix retains ChannelId, EdgeId, source/sink intention, mixer values, FX chain/binary state and logical routes; offline view honest |
| IO10 | User prefers direct low latency | With secondary features off, primary ASIO path bitwise/semantically equivalent to existing accepted path, zero new FIFO/ASRC |
| IO11 | Two independent DACs drive multiway speakers | Explain independent clocks and alignment limits; do not claim sample-accurate crossover unless common clock or physically calibrated delay and drift behavior have been proven |
| IO12 | One ASIO driver monopolizes output or rejects secondary input-only | Reject safely; no hidden dummy audible output or repeated driver-open loops |
| IO13 | Device graph config changes during audio | CONTROL-serialized atomic staged preflight/readback/publish/rollback; no UI 'ACTIVE' from mere request or callback count |
| IO14 | Several outputs but one VST chain | Each ChannelId FX graph processes once per master block; output sinks use precomputed post-processing signal, not duplicate rendering |

## GUI/UX (existing approved /preview/ only)

- Keep same console visual, FX racks, hardware PNG controls, faders, WIRE and native click behavior. Add compact configuration in existing AUDIO I/O overlay: **Clock Master / Primary Output**, **Capture Devices**, **Additional Outputs** and **Physical Pin Matrix** without a new top-level window.
- Device rows show actual backend, driver/device identity, negotiated rate/buffer, active pin count, qualified capability, and ACTIVE / VERIFYING / DEGRADED / REMAP REQUIRED / UNSUPPORTED. A source picker groups verified device + exact pin and searchable named mono/stereo aliases.
- Channel OUTPUT: keep distinct **INTERNAL WIRE** and **EXTERNAL HARDWARE SEND** semantics. Additional sink may be assigned a selected valid Channel/BUS post-processing tap. Do not invent new MAIN faders, duplicate hardware pins or silent bus redirects.
- Show a **Clock & Latency Inspector** only in advanced mode: per capture and sink clock domain, measured/driver-reported/estimated latency classification, current FIFO fill and corrected ppm, underflow and driver state. The casual operator need not understand ppm.
- Native request -> PENDING -> matching ACK + installed readback -> ACTIVE. Readback must identify **actual** device and source/target signal, not only a valid dropdown selection.
- Adding an optional device must NOT require globally muting and rearming a verified, already-playing primary output if a safe non-destructive transaction can be proven. On safety uncertainty, preserve primary and reject the secondary addition. Never silently switch the physical amplifier.

## Product boundaries

No custom kernel driver; no mandatory third-party virtual mixer or signing service; no hardware phase-coherence guarantee; no arbitrary 64/128 sample promise; no automatic ASIO cross-vendor hotplug success; no WebAudio DSP; no second native FX/VST3 renderer; no separate UI theme/rebuild; no change to SonKu/ImmerSIB voicing.

Existing output1 MAIN mirroring is retained as a useful compatibility mode. A generic additional sink supporting BUS/CH is **new work**, not proof the current output1 already does it. Device support displayed only from tested capabilities and real driver readback.

## Maintainability, MVVM and cross-thread product requirements (2026-11-18)

**User-visible goal:** adding future device, channel-routing, FX, monitor and speaker-output features must become routine and safe, without rewriting Main.cpp, the DSP callback, the approved UI, or another feature's code. **This is a product requirement, not a claim that source was already refactored.** The decision is an incremental JUCE C++ modular monolith plus a WebView2 *presentation* MVVM layer, documented in the [ADR](architecture/ADR_MODULAR_MVVM_FEASIBILITY_20261118.md).

| ID | Required operator/developer outcome | Acceptance and disallowed shortcut |
| --- | --- | --- |
| MA01 | One native accepted state after every GUI action | Selected device/source/output appears ACTIVE **only** after correlated requestId, native ACK and matching installed graph/device readback; no second JS authoritative mixer |
| MA02 | New I/O option does not force an unrelated feature rewrite | Device/source/output changes isolate in device application owner, source/sink binding compiler and existing GUI selectors; regression review records unrelated files touched and why |
| MA03 | Multi-output choices remain understandable | Operators can distinguish primary device pins, OUTPUT1 MAIN mirror, and independently selected BUS/CH sink; no phantom 8-output pins or invented latency |
| MA04 | Source disconnected, session survives | Keep named physical intent in `.smix`, independent of logical ChannelId/EdgeId/FX; offline and needs-remap are distinct from running PCM |
| MA05 | UX remains usable for existing one-device users | Existing `/preview/` layout, workflow, PNG hardware controls and primary ASIO direct behavior preserved; no new mandatory driver wizard, latency or MONITOR ritual |
| MA06 | Features are independently testable | Pure ViewModels (Device Setup and Source/Output Picker) tested without browser hardware; native command/graph/session tests separately; real ASIO/FAT independent |
| MA07 | Old and new threads reconcile without overwriting | Before edit: refresh main SHA, active PR HEAD and overlapping files; one scoped integration owner, targeted tests, no bulk merge/force-push of draft GUI/FX branches |
| MA08 | Feature status is not overstated | Publish distinct capabilities: primary ASIO multichannel, second-device INPUT FAT, independently routed OUTPUT FAT, combined I/O FAT; show unsupported vendor combination as UNSUPPORTED, not as success |

**UX behavior contract:** The existing AUDIO I/O area exposes distinct verified capture and output device rows, backend, connection status, rate and buffers, with compact native Source/Output selection. Advanced diagnostics can expose measured FIFO occupancy/correction, per-device latency provenance, capture discontinuities and output underruns without overwhelming the main console. Declared rate equality never implies synchronized hardware clocks. When a secondary device fails, only channels/outputs bound to it lose PCM; the primary mix remains usable where its device is healthy.

**Cross-device loudspeaker safety:** Independent USB output interfaces are not automatically phase coherent. For crossover or timing-critical multiway speakers, recommend a verified shared-clock multichannel ASIO interface or measured hardware-synchronization/alignment; do not advertise per-interface DAC sample alignment by simply adding a delay slider. Multiple simultaneous ASIO driver support remains contingent on hardware/vendor FAT.

**Definition of done per implementation vertical slice:** a short ownership map; native command -> validation -> session/routing/graph preparation -> publish -> actual device/source/sink readback; tests and negative paths; prior single-device parity; measured real-device evidence for claimed features; verified GUI behavior only when UI modified; explicit remaining hardware FAT. A doc-only or synthetic-only milestone is not a production audio release. Existing [maintainability epic #527](https://github.com/masarray/arsonmix/issues/527) already includes improvements: measure *remaining* coupling before coding new service/ViewModel layers.

## Release criteria

- **IO-G0:** architecture/ownership and unchanged single primary-device regression baseline recorded; no feature marketing before hardware proof.
- **IO-G1:** isolated exact-JUCE dual vendor ASIO + ASIO/WASAPI simultaneous capture and output probe, both open/stop orders, actual PCM evidence and format readback.
- **IO-G2:** deterministic per-input/sink SPSC and ASRC, hard RT, drift/jitter/44.1/48/96k+block variation, primary failure isolation, quality and CPU measurements.
- **IO-G3:** native prepared graph makes A+B input usable and routes a selected post-FX BUS to secondary output; accepted graph and VST3 once-only.
- **IO-G4:** approved GUI, native ACK/readback, stored .smix migration, missing endpoint reconnection and verifiable non-destructive recovery.
- **IO-G5:** Windows Release & CTest, exact compiled GUI smoke, hardware FAT >=8-hour sustained mixed source/output where feasible, explicitly recorded device compatibility matrix, independent UAT. Real ASIO FAT cannot be marked passed from simulated CI.
- **IO-G6:** product release only with reviewed JUCE/Steinberg ASIO open-source distribution obligations and no new kernel signing requirement.

Measurable pass gates are defined before running each physical rig: no second VST3 process, no callback heap/locks/wait, no unbounded FIFO drift, no silent reroute or lost sessions, separate provenance for capture underflow, sink queue underrun and actual OS render underrun (only when supported). Measured end-to-end latency, CPU budget and output alignment must be disclosed rather than invented.

## Coordination

This document is a **future full-I/O contract**; [PR #757](https://github.com/masarray/arsonmix/pull/757) covers secondary input, [current external output plan](architecture/MILESTONE_O_MULTIOUTPUT_NEW_THREAD_20261111.md) covers same-device ASIO output pins, and [maintainability issue #527](https://github.com/masarray/arsonmix/issues/527) covers refactoring. Do not merge all active PR branches or ship a new signal path until the exact native graph ownership has been inspected at implementation time.
