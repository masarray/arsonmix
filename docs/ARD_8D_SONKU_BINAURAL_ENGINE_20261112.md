# 8D-SonKu — Architecture Requirements & Decisions (ARD)
Version: 0.1 / proposed engineering architecture / 2026-11-12
Status: architecture design only, NOT implemented or physically audio-tested.
Repository audited: `masarray/arsonmix@1d84474661e1798b4a2f03bd8e6c9fbd7e1fb1fc`.
Guidance: root `AGENTS.md`, `docs/architecture/README.md`, `docs/architecture/FEATURE_EXTENSION_PLAYBOOK.md`.

## A. Architecture invariants / avoid naive implementation
1. Reuse ONE existing JUCE AudioIODeviceCallback and authoritative ChannelId/EdgeId PCM graph. This FX is an `AudioProcessor`/lease, not a new device/monitor/mixer/transport/JS audio engine.
2. Build high-quality *true binaural rendering*, not a panning simulator. Spatial localization = direction-dependent L/R HRTF/HRIR (ITD, ILD, pinna/torso spectral cues); acoustics = source-dependent early reflections + diffuse late reverb; effective externalization must be listening-tested.
3. Separation of concerns: independent pure motion math; dataset/backend; HRTF renderer; distance/direct-room; JUCE adapter; native editor; host session/preset adapter. No new global FX framework or per-plugin `Main.cpp` switch.
4. CONTROL thread does lookup, file IO, HRTF resample/precompute, immutable trajectory compilation, graph creation and release, validation/serialization; RT callback consumes only ready bounded storage and atomic mailbox/snapshot.
5. Preserve session/FX lease ID/graph revision/device epoch, existing rack bypass and accepted ACK/readback. Never show success from browser intent alone. Preserve old graph on failed prepare.
6. Prototype third-party HRTF facilities with a **replaceable backend seam**, benchmark before choosing a shipping dependency. Do not ship a known-unlicensed HRTF dataset/BRIR.

## B. Existing verified seams (live source)
| Existing owner | Correct 8D-SonKu integration |
| --- | --- |
| `src/NativeBuiltInFx.h` | One catalog entry `armix.builtin.8d-sonku.v1` with generic `prepareNativeBuiltInFx<SonKuSpatialProcessor>`, verified sample-rate/frame constraints. **Do not** edit Harmonice identity. |
| `engine/builtin_fx/BuiltInFxCatalog.h` | Optional canonical identity constant only; do not fork a second runtime registration registry. |
| `engine/builtin_fx/ChannelCompressorProcessor.h` etc. | Pattern for prepared `juce::AudioProcessor`, state validation, realtime control envelopes and metering. Not permission to copy DSP code or duplicate a transport. |
| `engine/core/LiveChannelFxGraph.h` / `DynamicChannelFxLeaseBank.h` | Reuse stable instances, move/reorder, prepared processing, bypass/PDC and retirement. |
| `engine/core/OrbitMotion.h`, `OrbitPhase.h`, `OrbitControlPolicy.h` | Existing pure vendor-VST parameter-orbit utilities: reuse conceptual phase/shape/test mathematics if suitable; **do not** treat VST-normalized positions as physical HRTF coordinates, or route through dearVR vendor parameter UI. |
| `src/NativeMixerSessionFxRestore.h`, `src/NativeMixerSessionStore.h` | Reuse generic native FX serialization and .smix roundtrip, schema/version validation. |
| `web/approved-native-bridge.js`, `web/approved-native-contract.js`, `site/preview-*/` assembled /preview/ | Native editor commands, accepted events and UI embedded in the one approved renderer. No second alternate GUI. |
| `tests/**`, `CMakeLists.txt` | Add targeted DSP/contract/native-host tests, not a duplicate full CI pipeline. |

## C. Proposed modular source ownership — descriptive paths, NOT files already written
```
engine/builtin_fx/sonku/
  SonKuTypes.h             // bounded state v1, source IDs, typed params, no UI
  SonKuMotion.h            // sample-clock phase, circle/ellipse geometry, easing
  SonKuPathCompiler.h      // CONTROL-only compile of path to fixed arc-length LUT
  SonKuHrtfBank.h          // immutable prepared HRIRs, orientation/metadata, lifetime
  SonKuSpatialRenderer.h   // bounded 1 or 2 emitter binaural direct sound
  SonKuDistance.h          // attenuation, coloration, direct/reflection ratio
  SonKuRoom.h              // prepared early reflections + stereo FDN late reverb
  SonKuCore.h              // processor-owned state + subblock render orchestration
engine/builtin_fx/SonKuSpatialProcessor.h  // JUCE native built-in FX adapter
tests/sonku_*_test.cpp                       // pure math, audio impulse, host/session
site/.../assets/mixer-native-sonku.js        // integrated assembled GUI editor ONLY
docs/PRD_8D_SONKU_SPATIAL_AUDIO_20261112.md
docs/ARD_8D_SONKU_BINAURAL_ENGINE_20261112.md
```
Keep source count proportional to actual responsibilities: start with `SonKuMotion`, `SonKuSpatialRenderer`, `SonKuCore`, `SonKuSpatialProcessor`; split a file only when it has independently tested ownership. Avoid heavy template frameworks, abstract factories or duplicate plugin-host layers.

## D. Signal flow / ownership
```
Existing FX insert / accepted ChannelId graph
  Stereo or Mono PCM
       -> input policy (mono=one emitter; stereo=L/R virtual emitters)
       -> sample-clock motion + source physical positions (no UI timer)
       -> prepared HRTF direction-dependent L/R direct filters
       -> distance gain/spectral shaping -> direct binaural ear sum
       -> optional source-dependent early reflections (bounded delay taps)
       -> optional shared stereo FDN / diffuse late tail
       -> spatial wet / dry equal-loudness blend + output trim
       -> normal channel FX rack/fader/WIRE/BUS/MAIN hardware outputs
```
`SonKuCore` is exactly one internal processor graph under existing host. No new ASIO/WASAPI owner. Early reflections must be based on room source/listener geometry, not a stereo-delay trick erroneously branded as realistic 3D.

## E. Coordinate and motion contract (unit correctness)
- Listener at origin, head fixed forward = +Y on top-down display; X positive right; Z positive up. Azimuth 0° FRONT, +90° RIGHT, ±180° REAR, -90° LEFT; positive azimuth = clockwise on the top-down graphic.
- For azimuth theta and radius r: `x=r*sin(theta)`, `y=r*cos(theta)`; `z=r*sin(elevation)` as appropriate when elevation is added. Treat physically meaningful meters separately from UI normalized radius. Avoid confusing legacy `OrbitMotionPoint.distance` normalized 0..1 VST parameter with meters.
- Two virtual objects for stereo: input L mapped to object L, input R mapped to object R with **linked** phase and user-defined angular spread by default; independent motion is optional later. One mono input maps to one emitter by default; never create two independent "speakers" from mono without explicit user enable.
- Sample-clock rotation: phase increments from the **number of rendered samples** and an explicit samples-per-revolution or tempo-linked beat count. Persist rotation state when stopping/starting according to documented pause/resume policy; modulo wrap is seamless.
- GUI head/source position is a low-rate non-authoritative snapshot. GUI render 30–60 FPS only for display; no JS setInterval audio steering. Drag requests are bounded and reconciled to native accepted state.
- Circle/ellipse mathematical evaluation is O(1); editable cubic path is compiled off-RT into bounded arc-length LUT (e.g. 1024 sample points; capacity/precision benchmarked) for approximately uniform audible velocity. Sample-clock phase indexing/interpolation O(1) in callback.
- Speed/radius/position/width changes are smoothed. Orbit shape switching uses clickless parameter morph or short position/filter crossfade, not an instantaneous HRTF impulse jump.

## F. HRTF renderer design and algorithm decision
- Input: 1/2 mono source streams. Each prepared emitter produces **both left and right ear outputs** via direction-dependent HRIR and ITD; sum into stereo with calibrated gain and adequate headroom. Plain equal-power panning is permitted only as an explicitly labelled non-binaural diagnostic baseline.
- Use `SOFA SimpleFreeFieldHRIR` as interoperable dataset input. Validation, dataset version/attribution/redistribution permission and resampling happen before publication; missing/corrupt/unsupported SOFA fails closed to a known safe preset or rejects the new prepared instance, never parsing in callback.
- Backend decision **OPEN pending benchmark**: (A) Valve Steam Audio CPU binaural API with vetted thread/allocation/latency behavior and Apache-2.0 distribution, or (B) small deterministic native FIR/partitioned renderer driven by licensed SOFA HRIR and libmysofa (BSD-3-Clause) control-side loader. Do not commit to either from marketing alone. Benchmark direction interpolation, per-block cost, allocations and timbral artifacts before adopting.
- Sample-cost sanity: 2 sources x 2 ears x 128 FIR taps x 48,000 samples/sec = 24.576 million MAC/sec before interpolation/room/overheads. This arithmetic is an approximate *cost model*, not measured project performance.
- HRTF orientation interpolation: choose smoothly interpolated HRIR/ITD, not abrupt nearest-direction jumps. Evaluate minimum-phase and explicit ITD separation vs direct HRIR interpolation to reduce artifacts. Filter update at bounded audio subblock rate (candidate 16–64 samples), with time-domain crossfade/smoothing where needed, not reloading HRIR each audio sample.
- No HRTF dataset guarantees all users can localize front/back. Provide HRTF selection and listening calibration as a product path, rather than exaggerated fixed spectral EQ. Avoid the claim that a static generic HRTF alone will always externalize.

## G. Acoustic space / room model
- Stage M1 has no room dependencies; truthful static HRTF must work first. M3 adds first-order shoebox/image-source reflections for limited virtual wall surfaces, using prepared delay lengths/gains/lowpass and direction-dependent early spatial cues. Parameter updates are staged and smoothed.
- One efficient **stereo shared FDN** reverb per FX instance; preallocated lines and diffusion/damping, no per-object long convolution tail by default. Keep time-varying early reflection contribution and late diffuse field separate.
- Head-relative source distance affects direct level, spectral/air cues where justified, and direct-to-reverberant ratio. Avoid pure exponential rolloff that only makes source quieter but never more external.
- `Room Mix=0` eliminates room components while preserving HRTF direct sound. Clear/retain reverb tail bypass behavior explicitly: preserve tail when power off only if host has a supported continuous-bypass contract; otherwise short fade to dry with no unbounded rendering.
- A premium measured long BRIR convolution mode is P2/offline until CPU, latency/PDC, license and user outcome justify it. No realtime ray tracing in initial shipping engine.

## H. Realtime and lifecycle rules
- CONTROL owns `prepareToPlay`, process format validation, preallocated working buffers, HRTF dataset load/resample, path LUT compilation, preset state parse/migration, deferred destruction and editor lifetime. RT processing is fixed-size, exception-avoiding and free of memory allocations, filesystem, locks, logging, network, JS and dynamic HRTF object creation.
- Rely on existing `RealtimeNativeFxControl` / bounded queues and generic FX lease/revision fences. No new global queue infrastructure unless existing API cannot satisfy bounded state. Parameter burst must coalesce/reject safely and never corrupt accepted control state.
- Publish changed heavy objects as immutable prepared structures at safe graph/FX control boundary; old object lifetime must outlive callback readers. No destroying old HRTF/FDN in callback. Fast continuous parameters should use the existing per-processor realtime control route, not rebuild FX graph on every knob tick.
- Set finite parameter ranges; clamp only well-defined input controls; invalid NaN/Inf, malformed number, impossible size/rate, stale slot/epoch, incompatible mono/stereo and absent HRTF produce explicit negative ACK or deterministic fallback. Never silently reinterpret a rejected request as completed.
- FX process: handle arbitrary block segmentation up to host prepared maximum, silence, 0 frames if called, mono input or stereo channel, denormal flush, tail reset, rapid wet/dry, pause/restart, sample-rate reprepare. Processor publishes true `getLatencySamples`; if host PDC insufficient, disallow selected heavy quality mode until safe.
- Proper host rack bypass returns dry under existing contract. In a spatial mix, doubling dry+binaural may cause comb filtering; use a perceptually appropriate crossfade and test both mono fold and peaks.
- On device loss, logical state survives, no "Monitor Off" gate introduced by the effect. Reprepare once for new epoch. Never bind to physical pins or create a separate device callback.

## I. Stable model and serialization
```text
SonKuStateV1:
  catalogId="armix.builtin.8d-sonku.v1"; schemaVersion=1
  enabled; spatialMix; outputTrimDb
  inputMode {autoMono,linkedStereo,independentStereo}
  positionA/B {azimuthDeg,elevationDeg,distanceMeters}
  motion {staticOrOrbit,shape,phase,periodSeconds,direction,startAngle,spreadDeg}
  shape {circleRadius,ellipseAxes,advancedControlPoints[bounded]}
  hrtf {profileId,qualityMode}
  room {presetId,earlyLevel,lateLevel,decaySeconds,roomMix}
```
Exact accepted native state serialization uses bounded versioned binary/typed codec, with finite/range validation and deterministic default migration. Persist pose/path and source identities, **not** executable code, HRTF sample arrays, detector history, live device identity or transient GUI pointer state. `.smix` retains effect state using generic existing FX restore; separate `.armixfx` user presets use the shared future preset library, not localStorage or plugin-owned session files. Stable FX instance identity follows move/reorder; no new ChannelId.
Acceptance = validated control ACK + accepted DSP readback for installed graph; logical-only status does not claim PCM. No GUI-only simulated audio engine.

## J. Testing matrix (targeted and low-waste)
| Test layer | Minimum contract |
| --- | --- |
| Pure motion tests | 0/90/180/-90 direction, CW/CCW, wrap continuity, phase partition independence, varied block sizes, ellipse and arbitrary path arc-length consistency. |
| HRTF dataset tests | SOFA orientation, sample rate resample, stereo ear ordering, 0 vs 180 HRIR not swapped, front/rear directional spectral distinction, invalid/out-of-license dataset path. |
| DSP PCM tests | Static impulse per cardinal direction, ITD/ILD bounded, no NaN/denormal/clip on test fixtures, dry bypass, wet gain, silent input and arbitrary chunk sizes. |
| Motion audio tests | Direction reversal, stop/resume, tempo change, shape edit, rapid phase 0/1 crossing, front-rear interpolation without clicks. |
| Room tests | Early reflection delays/gains respond to source position; room dry, tail behavior, FDN stability/decay, no runaway feedback; source near/far clarity. |
| Host integration | Real native insert on CH05 and BUS, rack bypass, other FX order, source-only/WIRE, .smix restore, FX move identity, device epoch/rebind, negative ACK state unchanged. |
| UX | Approved /preview/ editor screenshot at tested sizes; drag/click/keyboard hit zones; accepted readback and failure; no Chrome context menu or separate preview. |
| Performance | Bench p50/p95/p99 on documented hardware at 44.1/48/96k, 64–512 frames, 1/2/4 FX instances, allocation observer, actual ASIO XRUN/crackle FAT. |
| Perceptual | Volume-matched blind 4-direction localization, confusion matrix, externalization/distance/timbre rating on diverse headphones/listeners, generic vs selected HRTF. |

A synthetic PCM green test is not a real listening PASS. Keep docs-only PR without expensive CI; M1 native code gets fast tests then one exact-SHA native or FAT lane where justified.

## K. Design decision log / unresolved evidence
| Decision | State | Exit evidence |
| --- | --- | --- |
| Single existing native JUCE FX ownership | FIXED | Existing source and architecture governance. |
| Physical azimuth and 2-emitter semantic model | PROPOSED | DSP phase/IR tests and usability review. |
| Steam Audio CPU backend vs SOFA-native FIR | OPEN | Comparable CPU, fidelity, latency, license, callback safety benchmarks on reference rig. |
| HRTF dataset packaging | BLOCKED on license | Individual dataset license, redistribution, attribution and version pin. Source-code license does not license dataset. |
| Room FDN and early reflection algorithm | PROPOSED | Offline impulse/perceptual A/B and CPU profile. |
| Editor graphical path precision and gesture contract | P1/P2 staged | Real browser/native integration, pointer screenshots and accepted event tests. |
| Performance threshold / sample-rate range | PROVISIONAL | Exact-SHA compiled native CPU and physical hardware evidence. |
| Absolute claim of 3D externalization quality | NOT PROVEN | Multiple-listener headphone tests meeting PRD gates. |

## L. First implementation PR plan (after R0 documents)
One coherent M1 PR should add: minimal pure `SonKuMotion`, real prepared directional HRTF renderer, native `SonKuSpatialProcessor`, single `NativeBuiltInFx.h` registry entry, focused DSP PCM/host bypass/restore tests; touch approved GUI only when first audible insert is actually wired. Do not add CMake build cost, duplicate backends, empty scaffolding classes, fake front/back loudness or a second callback. Any feature not audible/testable stays out of M1.

See [PRD](PRD_8D_SONKU_SPATIAL_AUDIO_20261112.md), [research](RESEARCH_8D_SONKU_SPATIAL_AUDIO_20261112.md).

## Top-down visual orientation (2026-11-13 owner-approved)
The new photographed/illustrated blue listener-bust image is viewed from above with the person's face pointing to the BOTTOM of the SVG stage. Do **not** change DSP azimuth signs to match screen coordinates. Visual screen coordinates are X right, Y down, and projection is `screenX = centerX - radius * sin(azimuth)`, `screenY = centerY + radius * cos(azimuth)`. Acoustic FRONT=0° appears bottom, REAR=180° appears top, RIGHT=+90° appears screen-left, LEFT=-90° appears screen-right (anatomical right/left looking over the listener, not screen-side shorthand). Pointer inverse is `atan2(centerX - x, y - centerY)` and all changes still flow through the installed native FX ACK/readback; asset is only one passive GUI raster. The source PNG is `site/preview-v2/assets/sonku-listener-top.png` and is copied unchanged into the **one approved compiled /preview/** and native JUCE binary resource bundle. No DSP, HRTF dataset, routing, or extra HTML renderer changes.
