# ArSonMix — Smart Audio Engine: Implementation Blueprint v1.0

**Last updated:** 2026-10-25  
**Design authority:** [Smart Audio ARD v1.0](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md)  
**Source baseline:** `main@ab732c5`; experiment is not live-qualified  
**Related:** [P0 issue #15](https://github.com/masarray/arsonmix/issues/15), [P2.5 prepared handoff](P25_LIVE_GRAPH_HANDOFF.md), [GUI lock](GUI_BASELINE_LOCK_2026-10-25.md)  
**Order:** P0 diagnostics -> P0 backend/clock proof -> P0 adaptive bridge -> P0 FAT -> P2.5.2 live-safe FX -> P2.6 multichannel -> P2.7 sessions.

This is an **implementation contract and execution order**, not a feature-complete claim. Future engineers and AI threads must start here and explain any deviation in an ADR and benchmark evidence.

## 1. Repository-level module layout

Keep the current executable, `src/Main.cpp`, `src/LiveAudioEngine.h`, JUCE SDK and WebView2 until replacement slices are independently tested. Extract, do not rewrite the app in one giant commit.

```text
engine/
  core/
    RealtimeControl.h                     (existing; retain)
    AudioBlockView.h                      (new bounded spans, no allocation)
    PreparedMixerCore.h/.cpp              (pure DSP graph execution)
    ParamBank.h/.cpp                      (atomic scalars / sample ramps)
    PreparedGraph.h                       (immutable routing and metadata)
    GraphPublisher.h/.cpp                 (stage, retire, epoch/generation)
    SilenceConcealer.h/.cpp               (tested discontinuity handling)
  device/
    AudioProfile.h                        (capability / immutable session config)
    DeviceDiscovery.h/.cpp                (non-RT)
    DevicePolicy.h/.cpp                   (deterministic ranking)
    DeviceSession.h/.cpp                  (state machine / rollback)
    DeviceAdapter.h                       (backend-agnostic interface)
    JuceDuplexAdapter.h/.cpp              (first production backend)
    ClockDomainBridge.h/.cpp              (only for proven dual clocks)
    StreamingResampler.h                  (policy + streaming interface)
  plugins/
    PluginLifecycleCoordinator.h/.cpp     (affinity, ownership and cancellation)
    GraphCompiler.h/.cpp                  (non-RT topology & buffer plan)
  telemetry/
    AudioHealthCounters.h                (fixed RT atomics and event records)
    AudioHealthReporter.h/.cpp            (off-RT aggregation + provenance)
  test/
    FakeCaptureClock.h                    (deterministic drift / channel count)
    FakeRenderClock.h                     (schedule / jitter / buffer requests)
    AudioContinuityOracle.h               (PCM sequence + discontinuity detection)
tests/
  smart_device_policy_test.cpp
  smart_clock_bridge_test.cpp
  smart_audio_lifecycle_test.cpp
  smart_graph_reclamation_test.cpp
  smart_audio_contract_test.mjs
src/
  LiveAudioEngine.h                        (thin facade, gradually deprecated)
  Main.cpp                                 (window + native message dispatch)
web/                                        (LOCKED GUI v0.8; do not restyle)
```

**No microservice/process/thread explosion:** `*.h/.cpp` names denote logical responsibilities. A 1-file lightweight class is acceptable where smaller and testable; no generic dependency injection framework needed.

## 2. API and data contracts: exact proposed C++ shape

Build simple ABI-independent C++20 types. Names are proposed contracts, not implemented code:

```cpp
enum class AudioIntent { autoStable, lowLatency, advancedManual };
enum class Backend { asio, wasapiShared, wasapiLowLatency, wasapiExclusive };
enum class HealthSource { backendReported, measured, estimated, unavailable };
enum class SessionState {
    offline, enumerating, preparing, priming, running,
    degraded, recovering, stopping, faulted
};

struct EndpointCaps {
    std::string stableId, driverIdentity, backendVersion;
    Backend backend;
    std::vector<int> sampleRates;
    std::vector<int> supportedPeriods; // frames, queried not guessed
    int maxInputs = 0, maxOutputs = 0;
    bool hasHardwareSharedClock = false; // positive identification required
    bool hasInputClockMetadata = false;
    bool hasOutputClockMetadata = false;
};

struct AudioProfile {
    std::string captureId, renderId;
    Backend backend;
    double processingRateHz = 0;
    int requestedPeriodFrames = 0;
    int processingBlockCapacityFrames = 0;
    int channelsIn = 0, channelsOut = 2;
    bool crossClockBridge = false;
    AudioIntent intent = AudioIntent::autoStable;
    uint64_t deviceGeneration = 0;
};

struct AudioHealthSnapshot {
    uint64_t deviceGeneration = 0, graphGeneration = 0;
    uint64_t captureDiscontinuities = 0;
    uint64_t captureFifoUnderrunFrames = 0, captureFifoOverrunFrames = 0;
    uint64_t renderUnderrunFrames = 0;  // unavailable if backend cannot report
    uint64_t lateCallbackIntervals = 0; // estimate, never call xruns
    uint64_t dspOverBudgetCount = 0;
    double fifoFillFrames = 0, estimatedDriftPpm = 0;
    double processP99Us = 0, processP999Us = 0;
    HealthSource renderSource = HealthSource::unavailable;
};

// Direct path and bridged path share a single mix interface.
struct AudioBlockView { float* const* channels; int channelsCount; int frames; };
class PreparedMixerCore {
public:
    void prepare(int maxFrames, int maxInputChannels, int maxOutputChannels);
    void process(AudioBlockView in, AudioBlockView out) noexcept;
    // process NEVER opens or closes a device, allocates, logs, or mutates topology
};
```

`AudioHealthSnapshot.renderUnderrunFrames` must not report a fabricated 0 when `renderSource=unavailable`; JSON adapter should omit the field or emit null for genuinely unsupported metrics. Rate metadata separate from measured latency. Use `uint64_t` monotonic generation counters; never alias device generation to graph revision.

## 3. P0-00: reproducible glitch diagnosis (first PR)

**Allowed diff:** diagnostics + fake source + native status contract + unit tests + docs. No GUI v0.8 redesign and no production custom ASRC yet.

1. `output-only`: start **render only** (capture endpoint closed/disabled) and synthesize a bounded amplitude (-24 dBFS nominal, adjustable) sine/PCM, ramp in/out to avoid speaker pop. UI commands invoke on control thread; oscillator phase runs in RT but allocation-free.
2. `capture-only`: open capture without render when backend supports it, log event, counts, clock observation. If joined JUCE interface cannot meet mode semantics, report `unsupported` and add narrow proof-of-concept adapter.
3. `dry-through`: same input, skip plugin `processBlock`; measure entire I/O plus host DSP.
4. Add explicit in-app `MARK AUDIO GLITCH` diagnostic command without altering visual baseline; initially expose via a native-only diagnostic action or serialized log marker, not a new mixer modal.
5. Expose JUCE native `getXRunCount()` with exact backend meaning and unavailable sentinel. For pinned WASAPI code this is a **capture discontinuity indicator only**; it cannot account for all output starvation. Log actual callback frames and host processing time distribution rather than a single max.
6. A backend shim/patch may be necessary for `capture reservoir underflow`, `render padding low-water` and capture device-clock timestamps. Build a targeted, version-pinned adapter proof first. Do not quietly modify vendored JUCE or promise unsupported API observations.
7. Include source UUID for each observation, `deviceGeneration`, endpoint stable IDs (hash in portable reports), sample rate, profile, selected driver, time origin, callback count, health counters and output route; **never persist private device IDs publicly by default**.
8. CI fake device must simulate output-only & input-only. Validate zero DSP frames route and zero/no-false output on other channels. Include realtime memory allocation guard test.

**Exit:** 0-FX Windows EXE that can reliably distinguish output-only vs dry-through; 44.1/48k two-device reproduction with marked listening timestamps; no claim that the root cause is known until objective correlation.

## 4. P0-01: backend and clock-domain feasibility proof

**Decision tree before architecture complexity:**

```text
                          Endpoint selection / discovery
                                     |
                              same clock proved?
                               /           \
                             YES           NO / UNKNOWN
                             |                  |
                     direct JUCE duplex       independent stream adapter
                       no ASRC, no FIFO        capture and render timeline
                             |                  |
                          clean?             clocks observable?
                          /  \                /            \
                        yes  no              yes            no
                        |     |               |              |
                       FAT  inspect          proof-of-concept  explicit unsupported /
                            driver path      FIFO + ASRC       safe fallback
```

**Minimum feasibility spike:** two capture and render callbacks with independent timestamps, render clock master, bounded SPSC ring. Do not manufacture independent callbacks from one already synchronized JUCE callback. Compare against JUCE provided WASAPI/ASIO modes before writing low-level Windows IAudioClient code.

**Backend selection:** any use of native low-level WASAPI `IAudioClient3` must follow COM apartment, capability/period, stream-flag, MMCSS, event-driven lifecycle and device notification contracts. Reject requested periods not supported; no static 64-frame or arbitrary 480-frame assumptions. Keep JUCE as fallback and reference truth.

**Feasibility exit:** supported wired ASIO direct and wired/virtual separated endpoints can be driven by deterministic test adapters; no resource leak on cancellation or resume; allocation snapshots bounded.

## 5. P0-02: clock bridge and ASRC algorithm

Create a robust streaming state machine that is **not** confused with changing the PCM sample rate label.

Producer: frames/time stamps from capture device clock A.  
Consumer: requests at render clock B.  
FIFO: power-of-two fixed ring with read/write cursors, wrap-safe monotonic counters, strict single owner for each pointer. Stereo/mono channel map done without allocating. Prefer planar float32, with fixed maximum channels and frames per prepare.

**Per render block algorithm (conceptual):**

```text
fill = committedWriteFrames - consumedReadFrames
fillError = fill - targetFrames
measuredRatio = smoothedCaptureRate / actualRenderRate
correctionPpm = clamp(PI(fillError, slowWindow), -maxCorrectionPpm, +maxCorrectionPpm)
srcInputPerOutput = measuredRatio * (1 + correctionPpm * 1e-6)
SRC.processBounded(inputFIFO, renderFrames, srcInputPerOutput)
if underrun: count missing frames; apply defined output concealment; hold safe recovery
if overrun: count discarded frames; recover to target fill without uncontrolled repeats
publish occupancy, ratio, fault state in fixed atomics
```

**Control tuning contracts:** fillError sign above is positive when FIFO overfull (consume faster); integral anti-windup mandatory. Smooth corrections slowly compared with audio block rate. Choose startup `targetFrames`, `maxCorrectionPpm`, stability window and max FIFO size using generated tests plus measured hardware, not unsupported magic constants. Keep an explicit safe bound on latency from FIFO. On step change or resume, flush/reprime with de-click rather than letting the controller wind up for seconds.

**Example capacity calculation:** for a 48 kHz input and 20 ms target fill, nominal target is 960 frames, *not* total required capacity. Allow scheduled jitter, ISR/DPC stall, ASRC filter history, size limits and channel count separately; determine actual minimum via quantified tests.

**SRC candidate evaluation:** benchmark JUCE's native interpolators only if they meet streaming variable-ratio fidelity; compare against `libsamplerate` (BSD-2-Clause) or another permissive streaming FIR. Before adoption assert no heap or locks inside hot `process`; group delay, stop/start reset and max frame contracts; test with sine sweep/impulse/multitone and bit-exact direct fast path. Do not copy a closed proprietary resampler.

**Tests:**
- 44.1 -> 48 kHz and reverse; input stereo/mono, output stereo.
- ±100 and ±300 ppm fixed drift for ≥60 simulated minutes at 10× simulated realtime.
- Piecewise +/- rate drift, jitter and burst stalls 5/10/20/30 ms with exact expected frame accounting.
- Long-run 32-bit wrap simulations and tail wrap, high/low water hysteresis.
- No overruns/underruns in *steady-state cases within designed headroom*; controlled/concealed counted losses for stalls beyond headroom (do not require impossible unbuffered 30ms survival at 10ms target).
- Output continuity threshold via impulse discontinuity detector, RMS/sine distortion and NaN/Inf rejection; perceptual audio FAT separately.

## 6. P0-03: stable automatic device policy

No ML is necessary. Implement a **deterministic explainable policy** with observable measurements and a deliberately small candidate search space.

```cpp
struct PolicyDecision {
  AudioProfile selected;
  std::vector<AudioProfile> fallbacks;
  std::string reasonCode;     // CAPABILITY_DIRECT_ASIO, STABLE_SHARED, BRIDGED, ...
  uint64_t capabilityFingerprint;
  bool needsUserWarning = false;
};

PolicyDecision chooseAudioProfile(
  const DeviceInventory& devices,
  const UserIntent& intent,
  const LastKnownGoodStore& lkg);
```

**Candidate scoring dimensions** (no hidden magic weights): hard format/channel compatibility, single-clock availability, ability to share endpoint with streaming apps, measured stable profile history, declared monitoring-latency intent, system configuration and conservative period. Forbidden candidates removed **before ranking**. Always log the reason for rejected candidate.

**Fault escalation (illustrative, values calibrated by FAT):** `HEALTHY -> SUSPECT -> DEGRADED -> CONTROLLED_RECOVERY -> [HEALTHY | FAULTED]`. Distinguish genuine repeated render starvation from a one-time timing gap. Single confirmed fault alone may increment a counter, not force reopen. Implement cooldown between profile switches and a limited retry budget; freeze adaptation while a graph or device transition is active. For persistent hardware faults keep output safely muted and show concise reason; never enter infinite restart loops.

**Persist profile:** store stable endpoint/driver fingerprint, profile, test date and counters atomically in session metadata. Invalidate after device version change, changed sample-rate capability, endpoint replacement, repeated failures. Never silently force exclusive mode or change all other applications' Windows default device settings.

**Tests:** deterministic policy for no device, ASIO-only, shared-only, mismatched rates, 1/2 input channels, BT device with unavailable latency data, virtual capture, occupied exclusive endpoint, hotplug during prep, old-generation stale completion, cold boot/snapshot restore.

## 7. P0-04: hardening lifecycle, callback and observability

```text
message/control: enumerate -> validate -> prepare graph buffers -> open -> prime -> RUN
RT callback: fixed graph pointer -> fixed param snapshot -> DSP -> fixed event counters
device event: invalidate generation -> fade safe -> stop/close -> reenumerate -> reprepare -> prime -> RUN
shutdown: stop callback -> await epoch/stop acknowledgment -> destroy plugins/graph on proper executor -> release device
```

- Explicitly preallocate `scratch`, MIDI event capacity and plugin bus buffers; callback gets `AudioBlockView`, not `AudioBuffer::setSize` each cycle.
- Ensure `process()` can consume smaller final blocks and driver callbacks of varying size within `maxFrames`. If larger, bound and split with no allocations, **but** plugin must receive a prepared maximum compatible block size.
- Keep `float32` processing, finite-value sanitizer at designated boundaries only (per-sample sanitizer on every channel without evidence may be expensive), DC and gain ramps as explicit prepared nodes; optimize based on profile, SIMD only after demonstrated bottleneck.
- Separate RT scalar control atomics and discrete event dispatcher with correct ordering; panic master mute should have direct native atomic path and fail-closed fallback.
- Telemetry: atomic `last/max` plus fixed log-scaled histogram of per-block durations; capture backend-reported xrun from provided driver, exact FIFO frame loss from own adapter, render underrun only when observable (else `unsupported`). Rate-limit UI to ≤30 Hz and diagnostic disk writes to worker. Include audio callback execution and wall-clock gap as distinct metrics.
- Define a driver-runtime observability table in code: field, units, source, update cadence, reset policy, exactly what it does/does not prove. Include a diagnostics `testMode` field so synthetic sine never looks like microphone input in user UI.

## 8. P2.5.2: safe structural VST3 operations (after P0 FAT)

**No thread-affinity shortcuts**: Steinberg VST3 initialization/deinitialization normally on UI thread, with `process` on audio thread. Worker owns filesystem scan, hashing, buffer/graph planning. Plugin creation, setup, state restore and editor control must be marshalled to a compatible executor when required; keep UI responsive with nonblocking, staged state machine. Validate on FabFilter/iZotope and known problematic vendors.

```text
UI FX action -> validated ordered command + sequence
             -> dependency scan / graph compile non-RT
             -> plugin instantiate/state/load/prepare on affinity-compliant executor
             -> immutable PreparedGraph staged (one-slot/backpressure)
             -> RT fade/ack at block boundary
             -> retirement queue / generation ack
             -> vendor deactivate/release/delete on permitted thread
             -> UI success ack; failures leave old graph untouched
```

Graph topology immutable; live plugin DSP processor state is **not**. Do not clone arbitrary VST3 by memcpy; instantiate/restore legally and preserve vendor state. Avoid blocking audio when third-party process misbehaves; watchdog can mark offender but cannot forcibly preempt in-process `processBlock` safely. Optional VST3 process sandbox is later after requirements/latency model and security review.

At device rate/period change: coordinate graph rebuild with plugin lifecycle; never run `prepareToPlay` on a plugin simultaneously being processed. Add audio fade and latency compensation for mismatched parallel paths; show *measured* performance not fabricated blanket "seamless".

## 9. P2.6 true multichannel routing graph

After stable P0 and structural FX:
- Channel input maps from explicit validated capture buses. 12 strips and Master become actual DSP, not visual mock; all 6 inserts per strip are live only when engine path exists.
- Prefer dense indexed arrays and one prepared block execution order; stereo Master summing with bounded headroom and gain smoothing; pre/post metering with true physical provenance.
- Master FX and PAN become native or remain visibly marked preview until implemented; do not silently fake.
- Aggregate `LUFS-I` / `dBTP` according to ITU-R BS.1770/EBU R128 with qualified meters (later measured implementation), no browser-only simulation marketed as physical output.
- Avoid per-channel OS threads. Only consider graph parallelism if measured CPU deadline requires it and deterministic scheduling can be proved.

## 10. Repository PR ladder and acceptance

| PR / gate | Minimal scope | Required tests | Merge policy |
|---|---|---|---|
| P0-00 | output-only source, capture-only feature detection, diagnostic provenance | Windows Release/CTest, UI lock, fake I/O mode | merge after independent tests; mark diagnostic-only |
| P0-01 | device adapter seam + clock observation and JUCE fallback | different rates, reconnect, capture/render callback order | merge after safe backend feasibility |
| P0-02 | bounded clock bridge & benchmarked SRC behind capability flag | 60-min simulated drift, discontinuity, allocation guard, audio reference | **do not enable globally** until hardware A/B |
| P0-03 | AUTO/STABLE policy + rollback/hysteresis | property/state-machine and device matrix | enable only for validated profiles |
| P0-04 | actual wired + virtual/BT FAT and safety hardening | ≥30m 44.1/48k dry, 20 restarts, latency measurement | P0 acceptance gate, cannot CI-fake |
| P2.5.2 | safe add/remove/reorder+VST3 lifecycle | 12k+ swaps, vendor live FAT | only after P0 |
| P2.6 | 12-channel native routing, Master | routing/summing/latency/overload/FAT | only after P2.5.2 |
| P2.7 | snapshots and recovery | restart/vendor serialization/corruption | subsequent |

**At every PR:** base current `main`, do not overwrite visual v0.8; update module contract tests, CTest and `docs/...` evidence; Windows CI+Chromium PASS; document hardware coverage matrix. No merge of stale engine branches. Keep experimental release naming until physical acceptance.

## 11. Deterministic CI test rig and metrics

A fake capture/render backend uses **virtual sample clocks**, not real-time `sleep`, so simulations finish rapidly and reproducibly. Represent capture and render time independently (fractional fixed-point nanoseconds or rational frame counters), generate seeded jitter/loss and schedule callback actions in ordered simulated time. Feed integer-indexed PCM test patterns and check exact produced/consumed sequence, frame counts, generation acknowledgments and memory guards.

**Recommended CI matrix:** profiles for 44.1/48/96 kHz and varying 64/128/256/480/512/1024 callback frames if **supported**; independent clocks ±0, 100, 300 ppm; 1, 2 and 12-channel graphs; device restarts and rate changes during graph preparation; adapter input starvation and output-delay injection. No 96k acceptance claim without hardware qualification.

**Audio-quality oracle:** generate amplitude-limited sine/impulse/multitone, compare aligned output against analytic or offline high-precision SRC reference, measure RMS deviation, THD+N/alias suppression/overshoot under a benchmark protocol; thresholds selected only after baseline evaluation, with documentation. Check absence of discontinuities, clipping and nonfinite values. A listening FAT is still mandatory.

**RT-safety oracle:** global allocator instrumentation in test callback scope; fixed-capacity telemetry buffers; no uncontrolled locks, sleep, COM, file access, scan or virtual dispatch to UI. Instrument vendor processBlock separately; untrusted plugin cannot be guaranteed allocation-free without actual validation.

**Performance oracle:** CPU % mean/p95/p99/p99.9/worst, API type, driver + hardware, rate, period, route type, device-generation, graph size, DSP/ASRC group delay, measured RTL. Validate `p99<40%` and `p99.9<70%` of nominal deadline as **provisional lab thresholds**, stricter for supported release device classes. Never derive hardware xrun from callback timing alone.

**Failure handling test cases:** rate negotiation fails, device loses power, BT profile switch, capture FIFO overruns, render jitter exceeds target buffer, plugin editor closes during swap, worker finish arrives for retired graph generation, storage load corrupt, repeated retries, fallback fail. Output must be muted safely where appropriate and there must be no unbounded retry loop.

## 12. Hand-off checklist — every future thread

1. Fetch latest `main` commit + [GUI lock](GUI_BASELINE_LOCK_2026-10-25.md) + [smart ARD](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md) + this blueprint + [P0 issue #15](https://github.com/masarray/arsonmix/issues/15).
2. State exact current gate `P0-00` etc and evidence; do not assume design text is implemented or pass.
3. Scope work to **one vertical testable slice**. Name source files and contracts before code.
4. Preserve GUI v0.8 source, screenshot baseline and stable native mixer controls.
5. Include deterministic fake clock/no-alloc tests before enabling any adaptive mode.
6. CI: Windows MSVC Release, CTest, Chromium locked GUI. Keep logs/benchmark/artifacts from exact SHA.
7. **Physical FAT** must be reported as hardware-observed, not inferred from green CI.
8. Finish with PR, issue links, commit SHA, remaining risks and next gate. Don't declare generic "optimized" without profiling measurements.

## ASIO-first implementation update (2026-10-26)

Prioritize ASIO for live vocal/live mixer performance, with supported WASAPI direct and independent capture/render routes as peers, not replacement. JUCE 8.0.15 enumerates Windows Audio before ASIO by default; `AsioPriorityDeviceManager` now chooses first-run ASIO **only if exactly one matching input/output ASIO driver** is discovered. Driver sample rates and periods remain negotiated by JUCE without guessed settings; strict saved-route identity enforces user's manually chosen backend and device names. Without a saved route, failed automatic ASIO opening may fail over to Windows Audio DIRECT with an explicit diagnostic. ASIO path never enters independent ASRC. New deterministic policy and Windows compile-time + actual ASIO type registration test are required in CI. Never claim measured RTL from driver latency hints. Future work: ASIO device notifications, latency/FAT and real AUTO/STABLE low-latency health gating. Preserve existing GUI.
