# SonKu P9.4 — Front/Rear Externalization A/B (2026-11-16)

**STATUS: PRIVATE HEADPHONE FAT CANDIDATE / DRAFT, NOT ACOUSTICALLY VERIFIED.**

## Baselines / preservation
- Production main: `37985f02caecd93b22f853183888d81e18dc6736`.
- Operator-confirmed P9.1/P9.2: `763706a15a2617a10d23b5e2e43eb5689e454ccb`, frozen at `checkpoint/sonku-p91-p92-operator-confirmed-20261116`, issue #720.
- P9.3 GUI: draft PR #721; original perspective **ellipse** (148×83 SVG) is the APPROVED 3D oblique view of a circular 360° audio orbit. It is not the fault. Its movable modeless window, accepted FX Power, preset scenes and SPACE layout are inherited without changes.
- P9.4 starts from PR #721 head and adds *only* an explicitly opt-in spatial-depth experiment to the actual P9 native renderer. No upstream PR/main/checkpoint source is changed.

## What the listener actually reported
At equal physical source radius and continuous 360° orbit, front (0°) and rear (180°) are perceived too close to the listener while ±90° lateral positions feel farther away. A person perceives a *flattened acoustic orbit*, **not a faulty UI ellipse**.

The P9.1 renderer already uses a world-anchored two-speaker KEMAR 2×2 measured-HRIR field and LF foundation. It centres correlated stereo content at the actual median-plane HRIR. With a non-individualized HRTF, front/back spectral cues and distance externalization can be weak despite geometrically correct source angles. Older P9 direct/ER/tail levels have no head-relative angular externalization compensation. This is a perceptual limitation to investigate, not proof the DSP position calculation is elliptical.

## P9.4 A/B hypothesis and code contract
Research in binaural externalization relates localization to HRTF pinna fine structure, listener/head rotation, individualized headphone calibration, binaural reflections and direct-to-reverberant ratio (DRR); increasing diffuse reverb alone is not proven to fix it. A published 2026 frontal-source study also discusses direct and reflection level adjustment in generic BRIRs. Such research motivates a **bounded test**, not any claim of dearVR parity.

`SonKuP94FrontBackDepthCue.h` is a fixed-size, 45ms-slewed median-plane angular weighting computed on the existing 16-sample HRTF retarget cadence. It uses a 361-entry table prepared once, no allocation, locks, timers, FFT, or trigonometry in the per-sample audio callback.

- Head-relative centre azimuth: `p83::toHead(worldCenter, yaw)`. Never use SVG pixel coordinates or pretend the speaker moved closer.
- Weight: `cos^4(theta)`: 1 at 0° and 180°, 0 at ±90°; smooth and periodic at the wrap.
- For P9 only and if room + early reflection wet are ON: multiply the **already measured HRTF direct pressure** by `1-0.045*w` (max −0.40 dB), and its **directional early reflections** by `1+0.28*w` (max +2.14 dB). Late reverb, bass room modes, KEMAR spectral coefficients, ITD, ILD, source distance, orbit phase, output gain and stereo MID/SIDE graph are unchanged. The front/rear direct/early pressure-ratio proxy changes by at most about −2.54 dB. The ROOM OFF / ER=0 return is exactly neutral. This is **not** a measured personalized BRIR, not a cabinet resonance model and not a substitute for head tracking.
- The feature is compiled only with `-DARMIX_SONKU_EXPERIMENTAL_P94_FRONT_BACK_DEPTH=ON` and P9 already enabled. Default builds OFF, compile-time guard rejects P9.4 without P9.
- No state schema/ABI change: `renderMode=9` remains authoritative, but native readback `p94FrontBackDepthCue=true` specifically identifies this experimental build. UI label `P9.4 · FRONT/REAR DEPTH (A/B)` appears only for that genuine capability. P9.1-only builds still say P9.1+P9.2.

## Automated acceptance & safety boundaries
- Test real source PCM at 44.1/48 kHz, static 0/90/180/270 with ROOM ON/OFF, mono-like stereo and strict anti-phase, 64 vs 256-frame bit-parity, finite output + existing 0.98 linked peak guard.
- Isolate the curve itself: equal front/rear gains, lateral exactly neutral once smoothed, bounded ratio correction, head-yaw turns world FRONT into head SIDE, wrap continuity. Re-run existing P9.1/P9.2 PCM, P9.0 coherence, P8.9, GUI dropdown and Chromium tests.
- Exact source build of Windows native mixer + CTest and portable artifact must PASS before operator FAT. Keep draft, do not merge to main.

## Mandatory headphone FAT / rejection conditions
- Compare ***original checkpoint P9.1+P9.2***, PR #721 GUI-only and P9.4 at matching integrated loudness (±0.5 LU) on the SAME device/headphones. Play actual music: kick, bass guitar, low-level room content, vocals, percussive transients, and 30–180Hz anti-phase material.
- Orbit static stepped 0, ±45, ±90, ±135, 180; then continuous slow 6–12s with head yaw 0/90/180 and fixed 1.6/3.1m source distance. Record perceived distance (front/right/back/left), front-back reversals, tonal degradation, and whether room returns become echoes/boom.
- Inspect measured signal energy and short-time binaural interaural coherence per direction, direct/ER cross-correlation, potential transient smearing, limiter gain reduction, and true loudness against raw music.
- **Reject P9.4** if user perceives no appreciable improvement, music becomes more reverberant/distant but not externalized, integrated level moves >0.5 LU without intentional user gain, or bass/treble/phase and ASIO stability regress. Retain accepted checkpoint. Real HRTF personalization/head tracking/BRIR options remain future work.

## References / cautions
- Sound Externalization: A Review of Recent Research. https://pmc.ncbi.nlm.nih.gov/articles/PMC7488874/
- Auditory localization: a comprehensive practical review. https://pmc.ncbi.nlm.nih.gov/articles/PMC11267622/
- Enhancing Externalization of Frontal Virtual Sound Sources in Binaural Synthesis via Level and Reflection Adjustment of Non-individualized BRIRs (2026). https://doi.org/10.1177/23312165261468752

No claims that these articles establish the optimal numeric .045/.28 values: those are **research calibration candidates** requiring controlled audio comparison.
