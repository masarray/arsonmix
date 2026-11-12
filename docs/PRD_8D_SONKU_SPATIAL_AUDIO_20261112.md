# 8D-SonKu — Product Requirements Document (PRD)
Version: 0.1 / research and implementation-ready requirements / 2026-11-12
Product: ArSonMix internal built-in FX. Status: PROPOSED; NOT implemented, acoustically verified, or field-qualified.
Owner intent: convincing **front, rear, left, right, distance, externalization and moving 360-degree binaural sonic space**, not a stereo auto-pan illusion.
Naming boundary: **Harmonice remains the independent Vocal Harmony effect**. 8D-SonKu is its own brand, catalog ID, DSP, state and editor.

## 1. Product outcome and truth
An operator inserts 8D-SonKu on an ordinary mono or stereo mixer channel, selects a spatial movement preset, hears convincing binaural sound external to the head over headphones, sees a top-down listener/head and two moving source icons when stereo is selected, changes the orbit path by dragging it, controls speed and direction, and optionally selects a virtual acoustic room. All audible effects must run in the **ONE** native ArSonMix JUCE PCM callback/accepted FX graph; browser /preview/ is only presentation. Do not claim 3D realism merely because speaker dots move. Front/back discrimination, smooth motion, distance and listening tests are gates.

## 2. Target users / jobs to be done
- Singer/performer: one-click spatial vocal motion without opening a routing wizard or losing live monitoring.
- Music producer / stream creator: exciting stereo or mono sound motion at a controlled pace, in a selected acoustic room.
- Sound designer: freeform positioning, path shape, movement automation and repeatable saved presets.
- Advanced mixer operator: reliable CPU load, true rack bypass, state recall, sample-accurate timeline-based motion where provided, mono-compatible fallback.

## 3. Primary journey (P0)
1. Select existing CH or BUS -> FX Rack -> add "8D-SonKu" from verified native built-in FX picker.
2. Processor is prepared on CONTROL side, installed via existing FX lease. No device restart, special ARM/Monitor action, or second audio output.
3. Open the FX editor: head+headphones centered in large compact top-down visual; FRONT above, REAR below, LEFT and RIGHT explicitly marked. Stereo: independent L/R source glyphs, not physical speakers. Mono: one source glyph, or linked duplicated glyphs only when explicitly enabled.
4. Choose `Circle 360°` -> `Play motion`. Hear movement front -> right -> rear -> left, not just left/right loudness.
5. Drag source to change start location; drag orbit handles to adjust radius/ellipse; adjust revolution time, clockwise/counterclockwise, source spread, distance and wet amount. Editable advanced path is phase P2.
6. Choose virtual space (e.g. Dry/Open, Small Studio, Live Room, Large Hall), independently adjusting early reflections and late reverb via a simple Room Mix. Default room must enhance localization without drowning direct signal.
7. Toggle rack power/bypass, save/open .smix, reorder FX, switch input/device and reopen: same accepted state, no unsolicited route/graph reset. Audio is truthful only when physical route is installed.

## 4. Scope and priority
| ID | Priority | Requirement / operator-visible acceptance |
| --- | --- | --- |
| PR-01 | P0 | Verified native built-in FX picker entry `8D-SonKu`, stable `armix.builtin.8d-sonku.v1` identity, independent of Harmonice. |
| PR-02 | P0 | Genuine HRTF binaural direct sound for mono input and two virtual emitters for stereo input, stereo output to headphones. No mere equal-power pan marketed as 3D. |
| PR-03 | P0 | Clear independent FRONT(0), RIGHT(+90), REAR(180), LEFT(-90) static positions; direct positioning works when orbit stopped. |
| PR-04 | P0 | Orbit phase advances from the **audio sample clock**, not browser animation frames. Adjustable period, CW/CCW, start angle and stable pause/resume. |
| PR-05 | P0 | Compact top-down GUI shows listener/head with headphones, two stereo source glyphs following audible phase, draggable start points, circle radius/ellipse handles. GUI never drives PCM timing. |
| PR-06 | P0 | Smoothed position/HRTF/distance changes: no audible zipper, clicks or sudden dry/wet discontinuities; bypass remains safe with room tail policy documented. |
| PR-07 | P0 | Dry/direct vs spatial wet and output trim, conservative gain/headroom; quality checked on actual headphones. Mono and silence behave deterministically. |
| PR-08 | P0 | Native prepare, editable parameter ACK/readback, .smix roundtrip, FX bypass, move/reorder, graph revision/device epoch protection. No new callback or second master. |
| PR-09 | P0 | Headphone playback is clearly communicated. Speaker listening is not promised to provide spatial externalization. No false "headphone detected" claim. |
| PR-10 | P1 | Preset acoustic spaces: Dry/Open, Small Studio, Live Room, Hall; moving-source-aware early reflections, shared algorithmic diffuse reverb and controllable Room Mix. Original, not copied dearVR presets/IRs. |
| PR-11 | P1 | Subjective HRTF audition/calibration: front/rear/left/right examples and a small verified/licensed HRTF profile set with user choice; no assumption one generic HRTF fits everyone. |
| PR-12 | P1 | MIDI/host tempo sync where a reliable timeline exists; otherwise manual seconds-per-revolution remains fully functional. No inferred/fake BPM or GUI timer as audio clock. |
| PR-13 | P1 | Linked stereo orbit as default; independent per-emitter motion as an advanced choice. Preserve L/R source identity, controllable spread and downmix compatibility. |
| PR-14 | P2 | User-editable freeform closed Bézier path (bounded control points), normalized arc-length traversal and undo/reset; saved in plugin state. |
| PR-15 | P2 | Elevation/vertical orbit and optional genuine listener head tracking, only after basic horizontal externalization proves successful. |
| PR-16 | P2 | Offline high-quality HRTF/room rendering and user preset integration through shared native built-in FX preset service when it is production-ready. |

### Default settings (candidate; tuning required)
- Stereo Linked Orbit / Circle; 12 s per revolution; clockwise; headphone output; room Small Studio with restrained room mix.
- Source separation adjustable; center-front reference is azimuth 0 deg. Direct signal has priority over reflections/reverb. No hidden automatic head movement, no listener-mode auto-switch on device selection.
- `Dry` preset must actually disable room reflections/reverb while retaining directional HRTF; FX bypass must actually return unprocessed input through the host's normal bypass contract.
- All candidate defaults may change after loudness-matched listening validation, not through arbitrary visual tuning.

## 5. UX contract (compact and premium, no bulky cards)
- Main editor: restrained header with name / power / preset, **dominant circular trajectory stage**, head illustration, two small luminous L/R source glyphs, directional marks. Distinguish source positions and listener orientation; do not display two "hardware output speakers".
- Right/bottom secondary controls: `Motion: [Static | Orbit]`, `Shape: Circle / Ellipse`, `Speed: seconds per revolution`, `Direction: CW/CCW`, `Spread`, `Distance`, `Spatial Mix`, `Room preset`, `Room Mix`. Show only relevant fields. Use shared approved UI tokens, compact typography and appropriate control affordances.
- Dragging trajectory or source shows preview locally, then sends a bounded, validated native intent; accepted state/graph drives authoritative visible controls. Pending/rejected actions must be distinguishable without false audio claims.
- No default Chrome context menu, no new topbar mode, no new public /preview-v2/ product page. Exact native assembled /preview/ remains authoritative.
- Keyboard: pointer-free selection, arrow nudge, Enter accept, Escape cancel, Reset path; touch targets tested for mouse/touch and small windows.
- Static mode is first-class: pure front/rear placement must not require motion playing. Make A/B evaluation possible.

## 6. Nonfunctional requirements (measured, not assumed)
- Realtime: zero heap allocation, file IO, blocking lock, plugin scanning, HRTF parsing or GUI/JS communication inside PCM callback. Prepare on control side. No per-frame browser timer driving sound.
- Audio format: initial 44.1/48/96 kHz at 64/128/256/512 frames; later 32..192 kHz subject to actual DSP test. Unsupported formats reject **before** publication and preserve previously accepted graph.
- Deadline design budget at 48k/128 (2.667 ms callback period): **candidate** one instance 99th-percentile DSP <= 10% of block period on a published reference CPU, and 4 instances combined <= 30%. Update limits after genuine profiling, SIMD and device tests; percentages are engineering goals, not measured results.
- No unreported lookahead or unexpected host latency. If selected renderer incurs algorithmic latency, report and compensate via existing native PDC or reject until safe. Do not silently claim "zero latency".
- Deterministic offline replay for fixed rate/path/seed; callback partition invariance; finite outputs under silence, full-scale, denormals, corrupt/rejected state, repeated device reprepare and sample-rate changes.
- Explicit user-level latency, CPU quality and wet gain behavior; preserve independent ASIO primary / secondary stream bridge; do not alter device manager for this FX.

## 7. Perceptual release gates
- Objective: compare rendered HRTF/BRIR IRs, ITD/ILD and spectral cues, front-vs-rear distinctness, filter transitions, delay continuity, loudness and clipping against ground-truth target/known datasets.
- Blind headphone listening: static positions front/rear/left/right; randomized order and matched volume on multiple real listeners, multiple headphone models, both generic and selected HRTF. Capture confusion matrix (especially front/rear), perceived externalization and distance (1–5), tone coloration and preference.
- Candidate target **after HRTF selection**: >=75% correct front-vs-rear discrimination across eligible test listeners and median >=4/5 for externalization; report participant count, confidence interval and individual failures. This is an aspirational gate and may need revision; never report it as achieved without listening data.
- Motion: sound follows 360-degree path in listening test; no perceived discontinuities at angle wrap, ellipse extrema, direction reversal or period changes. Mono-fold and bypass A/B must not create destructive artifacts.
- Tests don't certify physical ASIO FAT. Release needs measured callback p95/p99, Xruns/crackles, device reconnect and headphone listening evidence.

## 8. Product boundaries / anti-requirements
No dearVR binary/source/reverb preset copying, unlicensed HRTF dataset distribution, false Dolby Atmos claim, full 3D ray-traced room by default, second JUCE AudioIODeviceCallback, new routing gate, automatic mic hardware rebind, hidden monitor switch, AI model, GPU dependency, speculative "all listeners" perfection, or replacing existing Harmonice Vocal Harmony.

## 9. Delivery milestones
- **R0 (this document):** verified references, PRD, ARD, repository seam, risks, performance and perceptual gates. **No claim of audible implementation.**
- **M1:** real 4-direction static two-emitter HRTF PCM core + native built-in insert, prepared state/latency/bypass, synthetic impulse and callback stress tests. Hardware/device unchanged.
- **M2:** phase-clock 360° linked orbit, circle/ellipse geometry, host parameter control, deterministic offline A/B PCM and static/moving audio fixtures.
- **M3:** original early reflections + shared FDN room presets, self-consistent distance, filter smoothing, loudness matching and real headphones front/back qualification.
- **M4:** native approved /preview/ editor + drag path UX, accepted ACK/readback, session restore, responsive screenshot/gesture qualification.
- **M5:** HRTF selection/calibration, independent stereo paths and advanced paths if proven useful, benchmark/tune CPU, integrated selective native/GUI CI and independent physical ASIO FAT.
- One scoped PR per meaningful *working* vertical milestone where practical. Docs-only change gets diff/references audit, no Windows CI dispatch.

## 10. Acceptance for first audible milestone (M1)
1. Insert native 8D-SonKu on CH05 -> FRONT input is processed to an actual stereo output path with measurable HRTF left/right filters, normal channel route and monitoring untouched.
2. 0/90/180/270 azimuth outputs are distinguishable in generated PCM/IR and finite; input silence remains silence, mono and stereo semantics are explicit.
3. Rack OFF produces tested dry behavior with bounded transition; restart and .smix restore retain settings or fail closed with no corrupted graph.
4. Deterministic callback tests at varied frames; no heap allocation/blocking in processBlock.
5. **No "3D verified" badge until blind headphone front/rear listening succeeds.**

See: [ARD](ARD_8D_SONKU_BINAURAL_ENGINE_20261112.md), [research evidence](RESEARCH_8D_SONKU_SPATIAL_AUDIO_20261112.md), and existing [feature playbook](architecture/FEATURE_EXTENSION_PLAYBOOK.md).
