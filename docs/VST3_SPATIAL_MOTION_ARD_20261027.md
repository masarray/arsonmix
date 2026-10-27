# ArSonMix VST3 Spatial Motion — ARD / Hardware FAT (2026-10-27)

## Scope and constraints
Keep the existing native JUCE VST3 host, approved 10-strip GUI, WebView2 static-loader fix, portable storage, and P0 audio diagnostics. Do NOT add WebView work or dynamic allocation to the audio callback. Motion automation is 60 Hz **message-thread control-rate**, not sample-accurate DAW automation. Vendor smoothing and the -180/+180 seam require physical headphone tests.

## P0: preserve the currently running Orbit when ADD FX
- Root cause: `Main.cpp::addCatalog` previously called `stopOrbit()` unconditionally.
- `LiveAudioEngine::addVst3` appends a newly instantiated processor; the existing processor pointer remains live in the graph. Controller pauses only the notification timer while JUCE detaches/reattaches the callback for editing, then resumes from absolute wall-clock phase **without calling start() again** and without ending parameter gestures.
- On successful append increment graph revision, emit the new chain and fresh Orbit state. If add fails, resume Orbit too.
- On unrelated REMOVE or MOVE, pause and rebind the same processor to the new slot by pointer identity, stopping only if the *actual orbit target* is removed. Never dereference the previous processor after it is destroyed.
- Preset recall, device swap, and actual target deletion still stop modulation as a lifecycle safety gate; don't promise persistence through plugin reinstantiation until state migration is designed.

## Motion architecture
`engine/core/OrbitMotion.h` is the shared pure numerical model used by the controller and to sample preview paths (64 intervals, emitted only when state is requested). It maps parametric phase to x (left/right), y (front/back), z (elevation), Azimuth, normalized Distance and Elevation. The WebView never invents a different geometry.

| Index | Preset | Axes needed | Geometry |
|---:|---|---|---|
| 0 | Circle | Azimuth | x=sin(t), y=cos(t), z=0 |
| 1 | Front–Back ellipse | Azimuth, Distance | x=.43sin(t), y=cos(t), z=0 |
| 2 | Left–Right ellipse | Azimuth, Distance | x=sin(t), y=.43cos(t), z=0 |
| 3 | Circle · Rise/Fall | Azimuth, Elevation | unit circle, z=.30sin(t) |
| 4 | Front–Back · Rise/Fall | Azimuth, Distance, Elevation | preset 1 plus z=.30sin(t) |
| 5 | Left–Right · Rise/Fall | Azimuth, Distance, Elevation | preset 2 plus z=.30sin(t) |

Angle is determined by atan2(x,y), **not** directly by phase for ellipses; otherwise dots and audible azimuth would disagree. A phase-inverse maps the existing vendor azimuth to the ellipse start without a sudden azimuth jump. Distance/elevation begin with a 300 ms ramp from the existing parameter values to avoid abrupt activation. The 360-degree hard seam remains vendor-dependent.

Vendor distance and elevation are **normalized values**, not measured metres/degrees. The initial built-in mapping assumes larger normalized distance means farther and larger elevation means higher. The UI requires explicit parameter selection and a calibration checkbox for these modes. If a plugin lacks a genuine independently automatable Distance/Elevation parameter, its associated shape cannot START. No guessed parameter IDs, no fake DSP, no interpretation of an Azimuth Scale parameter as Azimuth. Vendor confirmation is required before certification.

## Animation
The 132px top-down path and moving dot are rendered with inlined SVG; the browser interpolates the **C++-sampled** path using phase+period+direction from the host status. One requestAnimationFrame loop runs only while the popover is visible. The dot illustrates the *commanded* motion path; this is not a spatial audio analyzer or measurement of true speaker/headphone position. A separate height dot shows rising/falling motion.

Material Icons: self-contained play_arrow/stop/close SVG paths (Google Material Icons, Apache-2.0; https://github.com/google/material-design-icons). No network, no font download, no heavyweight animation.

## Acceptance gates
1. Baseline Circle starts/stops, does not jump at start, and continues smoothly after adding another unrelated VST3. Check rear 360-degree seam by listening at safe headphone volume.
2. Front–Back and Left–Right should produce contrasting audible distance dynamics only after vendor Distance mapping is confirmed. If not, mark as unsupported; do not report as passed.
3. Elevation path is blocked if no automatable Elevation parameter is exposed.
4. Move/remove non-target plugin retains Orbit and follows the intended target; removing the active target stops and closes gestures.
5. No ghost orbits after plugin destruction, callback graph edits, audio device changes, or preset recall.
6. Verify Windows Release build + CTest + Chromium native bridge smoke; CTest uses pure math and executes under NDEBUG.
7. Verify CPU, WebView and audio glitches with real VST3 and interface; a CI PASS is not a live-performance certificate.

## Known future work
Per-FX multiple simultaneous LFOs, full-session Orbit persistence, calibrated vendor parameter profiles, sample-accurate automation and vendor seam workarounds are explicitly out of scope. Never claim ultra-low latency from message-timer motion math alone.
