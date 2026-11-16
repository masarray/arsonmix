# SonKu P9.5 — Cinematic Depth and Large-System Music (2026-11-16)

**PRIVATE TEST BUILD. Not a publicly released or acoustically calibrated dearVR replica.**

## Operator priorities and approved checkpoint
- Audio: SonKu's improved musical transparency and world bass must be **preserved**; increase the perceived front/back depth, physical sound separation and scale of an external sound system without reducing punch.
- GUI: the ellipse is the intended 3D camera projection; keep it. Freeze the moving speakers as soon as accepted native insert/rack power is bypassed, never run a screen PLL against a frozen JUCE azimuth.
- Stop exposing the history of experimental P8/P9 renderer modes in the end-user editor. The current P9.5 native mode is shown as `SPATIAL ENGINE` with musical acoustic SPACE and factory scenes.
- Operator-approved recovery SHA `763706a15a2617a10d23b5e2e43eb5689e454ccb`; P9.3 PR #721; P9.4 PR #723. This new branch starts from exact P9.4 SHA `766b8d722ac18354e5c8e6eaa4e81f40ba42bcc8`. Do not alter main/checkpoint.

## Reference model comparison
Research basis: dearVR PRO 2 exposes virtual distance, variable 6-wall directional early reflections, frequency filtering and 46 varied acoustic environments with positional, late-reverb and room controls. These are ideas to STUDY, not proprietary algorithms/IRs to reproduce.
- Dear Reality official press release: https://cdn.uc.assets.prezly.com/152c508c-5687-430a-a7f6-52c33f005556/-/inline/no/Dear%20Reality%20-%20dearVR%20PRO%202.pdf
- Official manual: https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf
- Binaural externalization review: https://pmc.ncbi.nlm.nih.gov/articles/PMC7488874/

## Native P9.5 architecture and compatibility
One JUCE PCM authority. Compile gate `ARMIX_SONKU_EXPERIMENTAL_P95_CINEMATIC=ON` requires P9.4 and is OFF by default. Native v11 schema is unchanged. Integer `spacePreset` IDs 0..3 retain exact historical semantics; only P9.5 compiler accepts additional IDs 4..7. The UI only exposes them when native readback truthfully advertises `p95CinematicVenues=true`.

Each additional scene uses one shared table to choose a *different physical room* and independent prepared multiband FDN decay and axial LF modes:
| ID | User-facing scene | Room dimension (m) | FDN LF/MID/HF T60 targets (s) |
|---|---|---|---|
| 4 | Large Studio | 10×8×4 | .92/.78/.52 |
| 5 | Cinema | 22×28×10 | 2.05/1.75/1.16 |
| 6 | Live Venue | 32×36×12 | 2.75/2.40/1.55 |
| 7 | Arena | 39×39×16 | 3.10/2.85/1.78 |

These are nonproprietary **parametric model targets**, NOT real-world measured impulse-response claims. The bounded 8192-sample ER ring is validated for every room/distance. LF resonators are parameterized from geometric axial harmonics, maintaining ear-separated world bass. New FDN coefficients are created only in `prepare()`, not in the audio callback.

## Musical factory scenes (native ACK required)
All six scene presets select mode 9, 100% native spatial mix and preserve MIDI/PCM graph authority. Explicit final output gain is a *creative choice*, not secret normalization; the existing linked 0.98 sample-peak guard limits output, especially on mastered hot mixes. Input clipping cannot be undone. No claim that +7dB necessarily improves quality.

| Preset | Scene | Distance | Early | Reverb | Creative output gain |
|---|---|---:|---:|---:|---:|
| Immersive Music | Cinema | 8.2m | 95% | 62% | +5 dB |
| Large Studio Punch | Studio | 3m | 75% | 32% | +4 dB |
| Cinema Grand Sound | Cinema | 9.4m | 98% | 70% | +6 dB |
| Live Venue Power | Venue | 9.8m | 100% | 80% | +6 dB |
| Arena Massive | Arena | 10m | 100% | 87% | +7 dB |
| Orbit Spatial Show | Venue | 9m | 96% | 72% | +5.5 dB |

Operator saved CUSTOM scenes are not overwritten. A factory-new legacy-default P8.2 state is automatically promoted **once**, after native acceptance, via one existing SPSC-set ACK/readback; a customized old state remains unchanged until the operator picks an explicit scene. The old mode selector is hidden, not deleted from the native ABI, so sessions still roundtrip.

## Acceptance gates
1. No speaker movement or back-and-forth jitter while insert FX power, rack power, or global FX is OFF. On re-enable, reacquire exact JUCE observed azimuth, smoothly animate without moving the actual source location.
2. Native 44.1/48k C++ PCM tests at each scene plus 64/256 host block parity, linked peak guard, anti-phase stereo, DSP finite, no staleness of previously selected scene.
3. Compare INPUT source RMS vs processed source at real operator presets; require output >= input on representative mixes whenever headroom permits, without more than occasional 0.98 peak clipping. Also test guitar, stereo bass, vocals, 10m rear/front perceptual positions.
4. Native schema v11 unchanged; legacy 0..3 golden PCM unmodified; old .smix readback and user session never silently reinterpreted. CSS ownership and Chromium pointer/slider/dropdown tests pass.
5. Physical Windows/ASIO test and loudness-matched A/B with dearVR are **still required** before any release claim; perceptual depth beyond dearVR is not proven by C++ tests.

## Rejection criteria
Reject/retune any scene that reduces audible bass transient punch, exhibits hard limiting at hot music gain, front/rear reversal, mono/anti-phase collapse, flutter echoes at long wall travel, FX power visual flicker, or persistent native pending/ACK mismatch.
