# ArSonMix — Smart Audio Engine ARD v1.0

**Date:** 2026-10-25  
**Status:** APPROVED ARCHITECTURE DIRECTION / NOT YET IMPLEMENTED OR HARDWARE-QUALIFIED  
**Applies to:** experimental native baseline `main@ab732c5060fdd63656a47cd59604a1c9f4fb0909` and subsequent changes  
**Product:** Windows-first VST3 live mixer, singer/karaoke/livestream use  
**Canonical issue:** [P0 #15 — zero-plugin crackling](https://github.com/masarray/arsonmix/issues/15)  
**Implementation authority:** [Smart Audio Engine execution blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md)  
**GUI authority:** [approved GUI v0.8 lock](GUI_BASELINE_LOCK_2026-10-25.md)

> The word "smart" means capability-aware decisions with measurements, explicit fallback and predictable recovery; it does **not** mean heuristic magic, invented xrun counts, universal zero latency, or "AI" controlling real-time threads.

## 1. Product contract and hard boundaries

**Normal workflow:** user selects input and output (or AUTO) and presses play/monitor. ArSonMix negotiates the stable profile, rate, buffers, channel mapping and recovery; professional manual override is optional. User never has to calculate clock ppm to make audio work.

**Guarantee model:** for an explicitly supported device/OS combination and measured workload, meet clean-audio acceptance gates. Unsupported, defective, disconnected or unserviceable hardware yields a truthful, graceful degraded/fault state, not a false promise that any Bluetooth/driver can satisfy arbitrary live latency.

**Primary priorities in order:** (1) no dangerous noise/clicks, (2) continuous correctly routed signal, (3) bounded and predictable latency, (4) fidelity, (5) CPU/memory efficiency, (6) convenience. A driver profile must not automatically sacrifice 1–2 to make a nominal buffer lower.

**Do not begin P2.5.2 live-safe structural VST3 operations or P2.6 true 12-channel implementation while dry 0-FX I/O FAT is red.** The P2.5 graph/GUI checkpoint remains protected. This document does not imply the implementation is delivered.

## 2. Evidence and diagnosis, not speculation

Physical evidence from 2026-10-25: crackling with 0 FX, 48 kHz/480 frames, WASAPI shared ("Windows Audio"), independent VB-Audio capture and KTV_BT-named render endpoint. Worst 0-FX host DSP callback measured ~68 us at 48 kHz against nominal 10 ms period; worst callback start gap ~21.495 ms; timing-only `callbackGapCount=0` by a very permissive 2.3x threshold. Existing log does **not** expose verified render underruns nor source clock drift. Neither "CPU cannot keep up" nor "Bluetooth confirmed faulty" is proven. Also tested 44.1 kHz and eventual three-plugin chain.

Source-verified implementation issues in `src/LiveAudioEngine.h`:
- one combined JUCE callback, no capture/render clock separation or stable playout policy;
- no true render starvation, reservoir zero-padding, rate deviation telemetry;
- hot ADD/REMOVE/MOVE currently use `removeAudioCallback/addAudioCallback`;
- `scratch_.setSize(2, frames, ..., avoidReallocating=true)` happens in each callback chunk: avoid even state-changing buffer operations by preallocating and reusing views;
- graph-preparation and device-change lifetime rules need explicit state machine, affinity and cancellation.

JUCE 8.0.15 source (`modules/juce_audio_devices/native/juce_WASAPI_windows.cpp`) has three distinct types: `Windows Audio` = shared, `Windows Audio (Low Latency Mode)` = sharedLowLatency, `Windows Audio (Exclusive Mode)` = exclusive. In shared mode default driver period is selected; capture-reservoir shortage may be zero-filled; `getXRunCount()` returns capture discontinuity flags, **not complete render underrun accounting**. The result is **a priority to instrument I/O**, not a finding that ASRC must always be enabled.

## 3. Non-negotiable technical architecture decisions (ADRs)

| ID | Decision | Reason / trade-off |
|---|---|---|
| AE-001 | Retain JUCE 8.0.15 pinned as current host baseline. Backend adapters behind interfaces; assess updated JUCE or targeted shim only if validated. | Avoid rewriting reliable OS glue without evidence or introducing an unmaintainable fork. |
| AE-002 | Use **render-device clock as playout master**. Shared-clock hardware full duplex uses a zero-extra-FIFO direct fast path. | Single master timeline for mix; no unnecessary resampler/latency. |
| AE-003 | Separately clocked input uses bounded capture FIFO + clock-domain bridge + drift estimator + streaming ASRC if established necessary. | Prevents systematic under/overflow of fixed-frame copy; cost is added buffering and SRC work. |
| AE-004 | Stable AUTO policy with bounded finite candidate profiles, hysteresis, cooldown and rollback. | Prevents silent constant reopen/configuration oscillation; never autotunes device from callback. |
| AE-005 | Audio callback does no allocations, destruction, I/O, mutex waits, device restarts, WebView calls, scans, vendor state, or plugin construction. | Deterministic deadline. Existing JUCE/VST3 internals must also be profiled; not asserted safe by code style alone. |
| AE-006 | Prepared, immutable **routing/topology** graph at buffer boundary; smooth atomic per-channel parameters; epoch/ack retirement. | Safe structural FX edits and scale to 12 channels; plugin-internal DSP state is mutable on its designated audio thread. |
| AE-007 | Plugin hosting respects VST3 main-thread affinity for lifecycle; graph worker can prepare CPU-owned data, and dispatch vendor calls to correct executor. | Arbitrary third-party setup on background workers can violate Steinberg contract; no blanket worker promise. |
| AE-008 | Telemetry is provenance-tagged: `measured`, `backend reported`, `estimated`, `unsupported`. | Timing gaps are NOT actual render underrun counts. |
| AE-009 | One adaptable core and separate platform/device adapters; build **modular monolith**, not per-channel worker threads and not premature plugin process sandbox. | Performance, maintainability, less IPC/thread overhead. |
| AE-010 | GUI v0.8 locked. No changes to `web/app.js`, `web/styles.css`, standalone HTML or visual geometry without new approval. | Preserve accepted product baseline while fixing engine. |

## 4. Target topology

```text
                       MESSAGE / CONTROL PLANE (never realtime)
 WebView2 v0.8 -> typed command adapter -> ControlCoordinator -> DevicePolicy
                                             |                |
                         device events -> DeviceLifecycle   ProfileProber
                                             |
                           PluginHostCoordinator -- PluginAffinityDispatcher
                                             |
                           GraphCompiler / Prepare / LifecycleOwner
                                             |
                        one-slot publication + generation & epoch ack
                                             v
INPUT ADAPTER(S)                          REALTIME PLANE                      OUTPUT ADAPTER(S)
[ASIO same device] -----------direct--> PreparedMixerCore -----direct----> [ASIO same device]
[WASAPI capture clock A] -> FIFO -> ClockBridge (ASRC if needed)
                                             |
                                             v
                                  PreparedMixerCore (clock B)
                                   12 strips -> buses -> master
                                             |
                        Render clock B <-----+-------> [WASAPI render clock B]
                                             |
                                   RT fixed-size event telemetry
                                             v
                           TelemetryAggregator / DiagnosticsWriter
                                             |
                                     readonly UI snapshots
```

**Important:** this is the target architecture. The current JUCE `AudioDeviceManager` duplex adapter may supply only a joined callback. Independent callbacks/clock-aware FIFO require an adapter feasibility proof, not inserting a ring buffer inside the existing joined callback and claiming that it solves drift. If the backend cannot provide capture/render timestamps/flow, build a narrow platform adapter with test seams rather than forking the whole framework.

## 5. Module ownership and API contracts

| Module | Required API / state | Thread ownership |
|---|---|---|
| `DeviceDiscovery` | enumerate, stable endpoint identity, rates, periods, modes, channels, latency, permissions | control/OS worker |
| `DevicePolicy` | immutable capabilities → ordered candidate `AudioProfile`; explicit constraints & reasons | control |
| `DeviceSession` | `prepare/open/start/suspend/stop/close`, device-generation token, rollback | control with backend RT hooks |
| `CaptureAdapter` / `RenderAdapter` | frames, format, precise timestamps **when available**, backend health | backend callbacks |
| `ClockDomainBridge` | `prepare(capacity, rates)`, `pushCapture`, `pullRender`, occupancy/ppm snapshots, discontinuity handlers | lock-free SPSC or explicitly ownership-safe |
| `RealtimeMixerCore` | `prepare(maxFrames,maxChannels)`; `process(FrameBlock, PreparedGraph)` | exactly one graph audio owner |
| `GraphCompiler` | topology, buses, scratch offsets, per-route latency, cycle rejection | non-RT |
| `PluginHostCoordinator` | lifecycle state, affinity dispatch, state capture, cancellation tokens | control/UI-affine |
| `GraphPublisher` | bounded stage, atomic callback acknowledgment, deferred reclaim | control producer + one RT consumer |
| `Telemetry` | per-domain counters, histograms, last-N timestamped events, provenance | RT fixed storage; non-RT aggregation |
| `NativeUiAdapter` | versioned status/ack and actual control truth, rate-limited | message thread |

Do not put domain negotiation, UI status string creation or plugin scanner logic in the `RealtimeMixerCore`. Realtime component must be testable without JUCE device, Windows or WebView.

## 6. Clock bridge details

**Direct path:** if same known hardware clock is shared, bypass asynchronous resampler and extra FIFO; bridge objects may exist but process is a no-op (apart from routing). Prove this via backend/device identity; two similarly named endpoints are not sufficient.

**Separate-clock path:** capture callbacks timestamp and write bounded frames to a preallocated SPSC FIFO; render callback consumes via streaming SRC. The output clock determines demand. Occupancy error measured against a target triggers a slow rate-correction loop (PI / bounded controller). Include explicit anti-windup, update cadence, hysteresis and min/max correction; tune based on stress tests, not unexplained production constants. Expected drift 100 ppm over one minute at 48 kHz is ~288 samples = 6 ms, so an uncorrected fixed-size FIFO will eventually fail. This arithmetic demonstrates why dynamic matching is needed, not the exact ppm range of a specific device.

**Resampler policy:** compare benchmarked streaming FIR implementations (e.g. libsamplerate BSD-2-Clause) against a controlled in-house implementation, with documented quality, group delay, variable-ratio API, allocation behavior and license notices. Do not assume library calls are realtime-safe solely from their name. Only choose dependency after bounded process-path testing; resampler state initialization on control thread.

**Concealment:** underflow creates timestamped event; apply short boundary-safe fade/recovery (or other objectively tested concealment). Never silently skip indefinitely, repeat random stale memory, or claim the missing samples were restored. Severe sustained skew enters degraded mode with an explicit user-visible warning. Overflow must discard according to defined policy and count exactly what was lost.

**Not a network jitter buffer:** Wi-Fi/WebRTC playout algorithms solve additional packet loss/reordering problems; borrow measurement/control principles, not their entire stack as an audio-device solution.

## 7. Smart profile selection

Default UX profile is `AUTO_STABLE`; optional `LOW_LATENCY` only on endpoints that passed capability/health validation; `ADVANCED_MANUAL` explicit override.

1. Discover endpoint stable IDs, read available modes/periods, mix formats, channel maps, render/capture latency hints, existing live device availability, and BT/virtual classification **only when verified**. Unknown is its own value; do not classify solely by device name.
2. Rank a small deterministic list: hardware full-duplex native ASIO (when user selected or device proves suitable) → supported WASAPI shared/low-latency variants appropriate to use case → stable cross-endpoint bridge; exclusive mode must be opt-in/clearly warned because it can block other apps.
3. Choose **supported** mix/processing rate; prefer 48 kHz for stream use only if actual endpoints and target graph permit. 44.1 kHz remains first-class. Do not resample just for a marketing "48k" label.
4. Start with conservative supported period/headroom, not lowest possible buffer. Warm and measure health for a defined window (to be calibrated in hardware). Only lower latency after stability is proven and user profile allows.
5. On confirmed repeated faults: gracefully mute/suspend at safe lifecycle boundary, close/reopen with next **previously validated** profile; use cooldown, retry ceiling and rollback. Never repeatedly change buffer while sound is playing.
6. Persist last-known-good device profile keyed by stable endpoint IDs + driver/API version + relevant capability fingerprint; invalidate on hotplug/driver change. Explicit manual choice always wins until invalid.
7. Do not switch unexpectedly between ASIO and WASAPI or hijack another app's device. Inform the user of fallback and changed latency.

**Capability profile is not a benchmark-based assumption:** supported device rate/period must be queried, not inferred from screenshot or a hard-coded global table.

## 8. Backend support policy and latency semantics

- **ASIO:** preferred for a single clocked professional full-duplex interface when driver truly supports the chosen profile; never route two unrelated ASIO drivers by pretending they have one clock.
- **WASAPI shared:** best interoperability; respect system mix format and engine-selected period.
- **WASAPI low-latency shared:** evaluate JUCE's `Windows Audio (Low Latency Mode)` and supported `IAudioClient3` periods, gated by measurements. Lower period can be *less* reliable.
- **WASAPI exclusive:** specialized explicit choice; unavailable if endpoint occupied and may silence other applications.
- **Virtual audio cable:** true cross-clock bridge only if needed and supported by adapter, detect underflow vs sample rate mismatch.
- **Bluetooth:** support best-effort stable playback where feasible, but do not market unmeasured Bluetooth as low-latency vocal monitoring; profile/HFP switching and radio/driver constraints can be outside host control.

Track independently: processing block duration, plugin-reported latency, capture/backend latency hints, render latency hints, bridge occupancy delay, ASRC group delay and measured loopback RTL. Never add unlike estimates and call the sum "measured round trip".

## 9. Reliability and safety state machines

`OFFLINE -> ENUMERATING -> PREPARING -> PRIMING -> RUNNING -> DEGRADED -> RECOVERING -> [RUNNING | FAULTED]`; also `STOPPING`.

- Every async task carries `deviceGeneration` and `graphGeneration`. A completion for an old generation is discarded, never applied to a reopened endpoint.
- Device restart is control-plane-owned; prepare scratch, resampler and VST3 plans for *new* rate/buffer, then stage and acknowledge; never call vendor `prepareToPlay` concurrently with its `processBlock`.
- Hot-swap retains existing audio until a fully prepared new graph passes validation; if that cannot be guaranteed, a safely faded brief mute is allowed and reported. Reject/retry overlapping swaps.
- Old graph and plugin instances are destroyed only after RT ack/epoch retirement on a thread compliant with vendor affinity.
- Panic Master MUTE must be effective even if WebView2 is stalled (native fallback); power-loss and crashes cannot be prevented by app alone.
- Telemetry and diagnostic writes must not block audio callback. No logs or JSON from RT; preallocated counters only.

## 10. Performance objectives and acceptance gates

**Engineering targets**, not achieved production facts:
- Dry signal: zero audible clicks/dropouts in 30-minute supported-device FAT at **both 44.1 and 48 kHz**, no VSTs; repeat with GUI activity, switching channels and stress on message thread.
- Full chain FAT: FabFilter/iZotope/RX + ArSonKuPik representative actual hardware plus dry-bypass A/B. 30 min sustained each supported category; graph switching/hotplug have documented behavior.
- RT allocations/host mutex/blocking/I/O: zero in normal active callback path under instrumentation. Plugins are third-party exceptions: measure/quarantine/blacklist nonconforming ones.
- Callback budget: p99 CPU time < 40% deadline and p99.9 < 70% **provisional evaluation gates for qualified hardware**; no over-budget callbacks on tested supported target profiles. Max and distributions reported, not only average.
- Drift bridge: within benchmarked bounded occupancy under ±100/±300 ppm simulated independent clocks, plus 44.1↔48 kHz, random ± scheduling jitter and 5–30 ms stalls; error/concealment counts exact and assertions against clipping, NaN, stale-pointer access, excess memory growth.
- Latency tests separate actual measured RTL from engine estimates; record profile and hardware.
- Device lifecycle: 100 automated hotplug/reconfigure simulated sequences, plus 20 physical stop/start trials, safe fallback.
- Cross-thread graph handoff: >=12,000 stress swaps and hot-edit tests; deferred vendor reclamation.
- Windows MSVC Release/CTest, Chromium GUI baseline, signature/protocol contract and Pages publishing all green; GUI assets hash protected.

**Public-live gate:** FAT pass is separate from CI. Until then status is `EXPERIMENTAL / NOT LIVE-CERTIFIED`. No claim of guaranteed compatibility with literally every driver.

## 11. Explicit anti-patterns

Do **not**:
- try random buffer lengths until a problem disappears, without a controlled test/telemetry;
- assume `callbackGapCount=0` or fast DSP means no output underrun;
- enable ASRC for single-clock paths or resample unnecessarily;
- dynamically allocate MIDI/FX/scratch or alter `AudioBuffer` size in process callback;
- put each FX or channel on an OS thread; plugin parallelism requires proven payoff and deterministic synchronization;
- detach the audio callback for routine insert/removal/reorder;
- scan plugins or deserialize binary vendor state from audio thread;
- use `std::shared_ptr` ownership where the last reference may destruct on RT;
- mutate a graph visible to callback; confuse immutable graph topology with mutable DSP plugin state;
- rewrite mature JUCE backend prematurely or allow a permanent undocumented fork;
- turn a diagnostic heuristic into a fabricated precise xrun figure;
- overwrite locked GUI v0.8 from older native branches.

## 12. Documentation authority and sources

**Precedence:** this ARD v1.0 and [implementation blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md) supersede incompatible speculative/past statements in the legacy v0.3 ARD. Historical docs remain for traceability, but are not authorization to mark missing features as shipped.

Primary sources reviewed:
- [Microsoft — Low Latency Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio)
- [Microsoft — IAudioClient3 and supported shared engine periods](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-iaudioclient3)
- [Microsoft — User-Mode Audio Components / MMCSS](https://learn.microsoft.com/en-us/windows/win32/coreaudio/user-mode-audio-components)
- [Microsoft — WASAPI Capture GetBuffer / DATA_DISCONTINUITY](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)
- [JUCE pinned 8.0.15 — WASAPI backend source](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp)
- [Steinberg — VST3 threading and processing](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/API%2BDocumentation/Index.html)
- [Steinberg — VST3 process/lifecycle FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html)
- [libsamplerate — streaming SRC candidate, BSD-2-Clause](https://github.com/libsndfile/libsamplerate)
- [Original hardware issue #15](https://github.com/masarray/arsonmix/issues/15)

**Known source limitations:** these references provide API contracts and plausible mechanisms, not proof of exact hardware fault in the user's BT/virtual-cable pair. The P0 instrumentation/FAT phase exists to establish that proof.

## 2026-10-26 decision update — ASIO is the principal live-performance backend

**Implementation slice:** `AsioPriorityDeviceManager` puts the JUCE ASIO device type ahead of WASAPI **only on a fresh default setup** and only when the installed ASIO backend advertises exactly one unambiguous, identically named input/output interface. This is not a guarantee that the registered ASIO driver is wired hardware or latency-certified. With multiple ASIO drivers, no automatic guess is made; the user explicitly selects ASIO in the native AUDIO SETUP. Any valid saved explicit ASIO or WASAPI DEVICESETUP remains authoritative. A failed saved ASIO route **does not silently fall back to a different device**. When a first-run auto-preferred ASIO driver fails, Windows Audio direct is available as a clearly diagnosed startup fallback, never a hidden change to a user's saved device.

Live backend status distinguishes `ASIO_DIRECT_ZERO_EXTRA_FIFO`, `WASAPI_DIRECT_ZERO_EXTRA_FIFO` and `WASAPI_BRIDGED_ASRC`. ASIO DIRECT uses the existing JUCE hardware callback and **no added resampler or capture FIFO**. Driver-reported capture/output latency hints remain separate from physical loopback/round-trip latency. Windows CI must verify the ASIO JUCE backend exists even on a headless runner. Exact sample rate and quantum are *negotiated by driver* or explicitly configured, never guessed from a nominal latency marketing target.

**Remaining P0:** ASIO hardware FAT, hotplug fail-closed backend ownership, production AUTO/STABLE policy and measured loopback latency. Live-performance support is a product priority, not hardware-certified merely by compilation. GUI visuals are unchanged.
