# PRD — ArSonMix Multi-Device Input Capture (Windows, JUCE)

**Date:** 2026-11-17  
**Status:** PROPOSED / architectural product contract; **not implemented or physically qualified**  
**Audited baseline:** `main@07cbb521fcbc173c7769001c89f321a88cd9db31`  
**Full I/O redesign (2026-11-18):** [draft PR #763](https://github.com/masarray/arsonmix/pull/763) adds independent output sinks and MVVM feasibility; this input-only PR remains narrower.  
**Companion:** [ARD](ARD_HARMONICE_MULTI_DEVICE_CAPTURE_20261117.md) · [Research](RESEARCH_JUCE_MULTI_DEVICE_CAPTURE_20261117.md) · [Architecture map](architecture/README.md)  
**Product:** ArSonMix mixer. Harmonice is the separate built-in harmony plugin. The historical filename is retained for incoming links. **Initial release target:** Windows. Other platforms require separate platform designs and tests.

## 1. Problem and measurable outcome

Today the normal DEVICE setup selects one active JUCE audio device/driver for the native mixer. Operators using two USB audio interfaces cannot patch each interface's live microphone/instrument inputs simultaneously into separate mixer channels. External ASIO aggregators or a manual hardware submixer are unacceptable as mandatory prerequisites.

**Outcome:** A verified primary ASIO (or supported Windows Audio) render device remains the sole audio clock and production mixer/render owner. At least one *additional independently clocked physical capture interface* is attachable without changing the accepted logical ChannelId/EdgeId graph or requiring another DAW, virtual driver, kernel driver, commercial driver-signing certificate, ASIO4ALL, or VB-Cable. All admitted inputs may route to different channels, FX/VST3, BUS and MAIN concurrently. Unavailable driver combinations must fail with a precise, non-destructive error.

**Terminology:** a 'device' means a real OS/driver-opened endpoint. A 'source' means device-qualified input pin(s) and optionally a friendly named mono/stereo alias. A 'channel' means a logical mixer ChannelId; neither a hardware device nor its callback ordinal is a channel identity. An ASIO driver's ability to expose several physical interfaces under ONE driver does not itself prove multiple simultaneous ASIO driver instances.

## 2. Personas, journeys, and operator-first behavior

| ID | User journey | Required observed result |
| --- | --- | --- |
| U01 | Interface A (ASIO, vocal+guitar) is already in use; user adds independent Interface B (ASIO, mic+keyboard) | Native inventory, supported profile, verify B; keep A MAIN/output and existing graph; B inputs appear as available SOURCES only after confirmed open |
| U02 | A Input 1 -> CH01 and B Input 1 -> CH05 | Both input meters show *real independent PCM*, both may play MAIN together, subject to explicit WIRE/MAIN authorization; active VST3 chains stay in the single render owner |
| U03 | Routine source assignment | CH SOURCE popover -> grouped verified device -> input; <=2 deliberate actions once configured; no recurring driver dialog/ARM/Apply for routine source switch |
| U04 | Stereo source on B, nonadjacent pins | Named pair preserves distinct L/R and actual callback order; rejects missing/duplicate/unavailable pins |
| U05 | B unplugged while singer uses A | B channels show SOURCE UNAVAILABLE and their PCM becomes silent with anti-click ramp; A/MAIN and internal logical session remain usable, no implicit cross-device fallback |
| U06 | B reattached or USB order changes | Verify driver registration, endpoint identity evidence, source pin signature and format; restore only when exact matching policy permits; otherwise REMAP REQUIRED; never reroute to an identically named stranger |
| U07 | B cannot open while A is live | Reject B and retain A; no whole-mixer reset, no graph/FX destruction, no optimistic LIVE badge |
| U08 | Save/reopen .smix with B absent | Full logical channels, FX, WIRE and intended physical source references persist; B displays OFFLINE; no fabricated PCM, no forced update of saved binding |
| U09 | Existing one-device user upgrades | Exact existing ASIO direct path is unchanged when secondary capture is off; no added ASRC/FIFO/latency or fresh setup ritual |
| U10 | All sources mixed | Single native FX/render graph, no second VST3 processBlock on capture threads; ROUTE ONLY, SOURCE ONLY, BUS, MAIN semantics identical to existing mixer |
| U11 | 44.1 kHz secondary and 48 kHz master | Negotiated rates recorded truthfully; ASRC resynchronizes with no long-term FIFO runaway on qualified hardware |
| U12 | Setup of secondary driver requires unexpected playback pins / exclusivity | Rejected or explicit verified safe profile; never open audible output without the safety policy, never silently take over the primary |
| U13 | Driver reconfiguration / power suspend | No audio callback device restart on RT; old driver closes on control owner; affected inputs mute; output authorization only restored per existing trusted-output rules |

## 3. Scope and non-goals

**MVP:** 1 primary full-duplex/output renderer + 1 additional real capture interface, e.g. ASIO A + ASIO B or ASIO A + WASAPI B. Mono/stereo channel sources and reported input pin masks; up to 16 channels per bridge subject to actual hardware and existing mixer limits. Extend architecture to N secondary captures without allocating one mixer graph, one VST3 instance or one worker per mixer channel. Third and fourth secondary devices are future qualification milestones, not promises.

**Out of scope for MVP:** a virtual ASIO/WDM driver exposed to other DAWs; aggregate outputs with sample-accurate alignment; proprietary driver installer; macOS Aggregate Device emulation; deterministic coherent phase alignment between free-running USB clocks; distributed recording with guaranteed cross-device sample alignment; Bluetooth monitoring latency guarantees; changing accepted /preview/ layout or SonKu/ImmerSIB DSP.

**Hard product limitations:** Some proprietary ASIO drivers are single-client, require duplex activity, prevent concurrent use with WASAPI, or crash on unsupported combinations. A UI '16 ASIO slots' claim is forbidden. UI must report qualification and allow safe rejection, not claim universal support. The independent clock path necessarily introduces buffering/latency, so do not market 'zero latency' for secondary inputs. The primary ASIO/direct path shall remain unchanged.

## 4. Existing design ownership and preservation

- Keep **one authoritative mixer renderer**, ChannelId/EdgeId/FX processing graph, FX plugin leases, and graph revision publisher. Existing additional capture callbacks are strictly I/O producers, not additional audio renderers.
- Preserve accepted `.smix` logical workspace separately from physical SourceBindings; device detach is not channel deletion.
- Reuse established device apply fencing, logical source picker and native intent/ACK/readback; normal operator path remains compact.
- Do not make duplicate SOURCE/patch managers or a separate WebAudio/JS PCM engine.
- Maintain intent semantics SOURCE ONLY and no MAIN until WIRE installed; input metering remains if a live input exists, even when disconnected from MAIN.
- Existing optional OUTPUT1 renderer remains a separate output feature with its own clock bridge/ownership; multidevice must budget its resources and not corrupt/duplicate MAIN playback.

## 5. Proposed Audio Devices UX (not yet implemented)

Within existing **AUDIO I/O / DEVICE** screen, show a compact **Primary Playback / Clock Master** row and a collapsible **Additional Input Devices** section with 'Add input device'. Each device row shows verified backend, driver registration/endpoint name, actual sample-rate, current input pins, latency class, and status: DISCOVERED / VERIFYING / ACTIVE / DEGRADED / DISCONNECTED / REMAP REQUIRED / UNSUPPORTED. Primary never silently changes when adding B. Secondary 'remove' detaches physical binding, not mixer channel or FX state.

The existing CH SOURCE selector groups under `Primary Interface > Input 1`, `Secondary Interface > Input 1` and user-defined aliases; show secondary approximate latency as an informational chip only where meaningful. Missing inputs remain listed but unselectable with a clear reason. Advanced view has FIFO fill, clock correction ppm, measured capture dropouts, p95/p99 callback-budget evidence, and actual backend/readback; no fabricated millisecond promise or hidden automatic fallback.

When the operator chooses a source, send one typed native intent with requestId, device-generation and expected logical/graph revisions, then wait for native accepted route snapshot. Device add/remove is a separate serialized control transaction. Driver-opening warnings are contextual and not a series of confusing checkboxes. Do not demand a repeated MONITOR toggle for a previously verified, user-requested playback path.

## 6. Nonfunctional requirements

- **Realtime:** zero dynamic allocation, mutex wait, file I/O, UI/WebView2 calls, COM/registry calls, driver open/close, DSP/VST3 lifecycle, or blocking device discovery on ANY capture/render callback. SPSC source queues have one named producer and one render consumer, with preallocated buffers and bounded work.
- **Continuity:** fail silent on stale identity, generation, missing buffers, format mismatch or stopped secondary; anti-click transition. A failed secondary must not block the primary callback or terminate unrelated native FX.
- **DSP fidelity:** sample-rate conversion uses band-limited interpolation with objective passband/stopband/alias testing, bounded drift correction, long soak and transparent telemetry; never naive memcpy or linear copy at mismatched clocks.
- **Latency:** publish separately measured buffer/device/ASRC/FX latency and estimate provenance. Primary direct path's processing overhead and output latency are regression-gated against matched baseline. Secondary latency is profile-dependent; no universal claim.
- **Persistence:** saved logical session survives device changes and absent interfaces, with versioned additive SourceBinding schema, deterministic upgrade from legacy single-device routes and round-trip tests.
- **Security:** no unsigned kernel driver, no silent driver class substitution, no trust based on friendly names, no arbitrary software source elevated into physical output approval.
- **Packaging/legal:** user-mode JUCE/ASIO host; verify JUCE 8 AGPLv3 and bundled Steinberg ASIO SDK GPLv3 obligations before publishing changed binaries; commercial kernel signing not part of architecture.

## 7. Milestone gates — no trial-and-error feature creep

| Stage | Deliverable | Go / no-go |
| --- | --- | --- |
| MDI-0 — Architecture | Research evidence + this PRD/ARD + conflict audit | Explicitly resolve 'one callback' vs multiple capture callbacks, device identity and separate clocks |
| MDI-1 — Isolated dual-driver feasibility | Windows native probe executable on exact pinned JUCE, separate driver objects/callbacks and synthetic PCM counters, no mixer integration | Concurrent open/start/capture/stop for **two distinct vendor ASIO drivers** on one qualifying test rig; report negative results as unsupported, not a guessed fix; no driver crash |
| MDI-2 — Stable physical capture core | Device-qualified identity, 1 secondary SPSC ASRC, bounded fault/isolation, deterministic synthetic tests | 44.1/48 kHz and realistic drift/jitter soak pass without queue runaway; no primary render glitch introduced |
| MDI-3 — Native graph admission | Immutable unified input mapping, one production render callback/FX lease, ACK and session binding | A CH01 + B CH05 routed simultaneously, FX+BUS+MAIN work; SOURCE ONLY behavior retained; exact-device rollback |
| MDI-4 — Operator UX and recovery | Approved /preview/ GUI integration, device and source picker, persistent `.smix`, reconnect/remap | Genuine native readback, no false LIVE, no lost Show/FX/mixer document; one-device path regression unchanged |
| MDI-5 — Windows production qualification | MSVC Release CI, ASIO+ASIO and ASIO+WASAPI real-device FAT, long-duration capture/monitor tests and release evidence | Field sign-off with device/driver/rate/buffer matrix, actual recorded observations; no claim of universal support |

**Release blocker:** MDI-1 cannot be bypassed by WASAPI-only simulated tests when advertising dual-ASIO. Hardware FAT cannot be replaced with DOM/Chromium screenshots or synthetic CI. A failed dual-ASIO probe is a compatibility finding with a supported WASAPI alternative, not a reason to inject unbounded workarounds into the production engine.

## 8. Acceptance evidence packet

A release candidate must attach exact executable SHA, JUCE revision, Windows version, driver names and versions, qualified hardware topology, primary/secondary negotiated pin masks/rates/buffer sizes, operator source mapping, clock fill/ppm and underflow/overflow counters, callback-budget histogram, recorded audible/dropout results, rollback/reconnect session test, and pass/fail CI links. Raw recordings optional and user-controlled. Exclude sensitive machine paths, registry class IDs or device serial numbers from public logs.

## 9. Traceability

Existing source: `src/LiveAudioEngine.h`, `engine/device/JuceIndependentClockDevice.h`, `engine/device/AdaptiveClockBridge.h`, `engine/device/LiveSecondaryOutputDevice.h`, `engine/device/LiveInputPortRegistry.h`, `engine/device/JuceActivePortSnapshot.h`, `engine/core/PreparedNativeMixerSession.h`, `src/NativeDeviceApplyCoordinator.h`, `src/NativeMixerSessionDocument.h`, `web/mixer-io.js`, `web/monitor.js`.

Related contracts: [device-independent session](ADR_DEVICE_INDEPENDENT_MIXER_SESSION_20261107.md), [custom device setup](PRD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md), [input routing](PRD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), [architecture ownership](architecture/MAINTAINABILITY_EXECUTION_V1_20261111.md). Their historical single-device policy remains valid in **single-device mode**; the companion ARD describes the strictly bounded multi-input extension.
