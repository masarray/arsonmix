# SonKu P7.1 — source XYZ / measured DIRECT HRTF, experiment

2026-11-14 | stacked PR #681 → P7 #679 → P6 #678 → P0–P5 #674.
**This milestone is research implementation, not Windows ASIO or dearVR
parity certification.** No main/installer change.

## Physics, source of truth and control-plane safety

The 44.1k/48k MIT KEMAR 14-band measurement bank already pinned in P7
is reused for the **direct** binaural FIR (L/R emitters *and* coherent
MID correction) with no extra convolution or per-tap magic EQ.
Source radial distance remains the original accepted 1.6–10m, projected
at a measured elevation to horizontal distance = r*cos(theta) and
source height = earHeight + r*sin(theta). First-order wall image sources
use (x,y,z) path length, arrival elevation/azimuth and measured KEMAR
coefficients, while floor/ceiling images use the true reflected source
height and subtract the **3D source radial path**, not an incorrect
horizontal-only direct distance. At elevation 0, original four walls,
P7 vertical reflections and direct measured horizontal bank are preserved.

A single `SonKuSpatialCore::setResearchDirectElevation()` accepts only
a pre-prepared `SixSurfaceKemarResearch` instance before *any* PCM
has been processed, validates source clearance within the current
3D room, rejects NaN and impossible positions, and does not add a
public FX parameter, .smix version, GUI gesture or audio callback mutation.
It is guarded by BOTH `SONKU_P7_MEASURED_ELEVATION_RESEARCH=1`
and `SONKU_P71_DIRECT_XYZ_RESEARCH=1`; neither appears in public JUCE.
Per-frame FIR tap count remains 140; moving source orientation is the
original azimuth clock and the new elevation is constant within a
quiescent research render. A real user-controllable XYZ scene requires
a **separate approved host UI/state/ACK design**, not an invisible knob.

## Evidence and disqualifiers

The native CI covers 44.1/48k, true distance projection, room-clearance
rejection, above/below path geometry and direct HRIR ear mirror,
mid/side phase and chunk-parity with 64/257 irregular partitions,
no post-publish changes, and prepared restart/downgrade to P6.
A/B/C 12 lossless original source 48k stereo WAVs compare horizontal
versus +25°/-25° direct placement in FRONT/REAR and 0/60° spread.
Goertzel offline instrument evidence measures 220/660/940/3200/4700Hz
per-ear level, interaural level and phase, flags significant spectral
changes for human review; these are not perceptual quality scores.

Performance measurement is only same-runner Linux C++ timing of 1/2/4
concurrent stereo instances; the new measured-elevation interpolation
updates every 16 direct/64 reflection PCM frames and introduces no new
FIR in the callback. This **does not** qualify p99/XRUN on physical ASIO.
No hidden adaptive DSP mode switch, plugin duplication, bit-depth
truncation, per-buffer heap/clock, synchronous logging or background work.

P7.1 source-level milestone can be marked complete after the exact
CI SHA passes, but public feature remains vetoed until actual
Windows ASIO, performance headroom, author-approved FX/UX workflow,
.lsmix/.smix persistence and headphone blind tests versus dearVR are
satisfactory. Do not merge stacked drafts prematurely.
