# SonKu P9.1 + P9.2 — Reconciled Music Fidelity and World-Anchored Bass

**Status:** PRIVATE HEADPHONE FAT CANDIDATE — NOT RELEASED, NOT dearVR PARITY

## Reconciliation (performed first)

- Starting P9 HEAD `cd85ffee054dad6b3b752b5351d34921ddc92717` (draft PR #717).
- Production `main` `37985f02caecd93b22f853183888d81e18dc6736` already contains #713 ASIO/WASAPI continuity, operator MAIN intent, audio graph/global FX and saved-session safeguards.
- New two-parent reconciliation commit `ea0a7c0a5bb3afb2c4295176447c2eb78bb6835b`, with main as direct ancestor. Imported the 30 main-changed files and applied 29 exact-context patch hunks for the four overlapping files: `src/Main.cpp`, `CMakeLists.txt`, `p1-windows.yml`, `mixer-fx-rack.css`. No production branch changed or merged.
- The new acoustic development and GUI belong to `research/sonku-p91-p92-fidelity-bass-reconciled-20261116`, PR #719, based on PR #717.

## Why the previous default was not acceptable

Old P9 used legacy output trim 0.75 (about -2.5 dB) and HRTF wet factor 0.5. Critically, P90 inherited P8.2 `transparentBass`: the low part of the world HRTF was *subtracted* and the original headphone-locked L/R bass was brought back. That violated the world-speaker premise. The original P8.2 bass-shelf 0.82 (+5.2 dB when fully active) was also still applied to the P9 branch with room ON, so a naive LF enhancement stacked gains and clipped musical intent.

The operator's field setting (82° speakers, near 1.6m, Large 28.8m room, +7 dB output, reflections 100%, reverb 62%) is a crucial listening reference; copying that full +7 dB and 100% wet as default would hide the bug.

## Integrated native DSP architecture

1. Keep the existing measured 2x2 KEMAR direct HRIR and P9 coherence-safe MID/SIDE compositor. Only P9 mode 9 routes into the new LF foundation.
2. Complementary two-pole lowpass at 165 Hz: `HRTF(high)=HRTF - LP(HRTF)` and `worldLow = W_L*LP(L) + W_R*LP(R)`. BOTH use the same lowpass topology, avoiding a disconnected high-pass and duplicated low energy. Source weights are `0.5*(1 +/- 0.27*sin(headAngle))`, with a 361-point sine lookup prepared once. They follow the existing world-speaker and head-yaw angles; no original headphone L/R dry-low return.
3. P9 only, use 0.72 scene binaural gain and no legacy room loudness multiplier 1.12 or legacy +0.82 LF shelf. The full original peak protection remains. Native P8.2–P8.9 behavior is unchanged.
4. Room EARLY now receives independent L/R with the actual P9 speaker angle. The eight-line late FDN receives the true L/R, not a duplicate MID source. Speaker azimuth and world/head rotation apply to the early image reflections.
5. Three lightweight axial mode resonators per room (Studio 7.2, Chamber 14.4, Large 28.8m) are computed from `f=n*343/(2*L)` nearest preselected LF clusters and a stable Q=2.0/2.8/3.8 bandpass. Bounded wet weights 0.14/0.105/0.075 and 60ms room/size smoothing; ROOM OFF is strictly zero. This is *not* a personalized/physical subwoofer driver/cabinet IR, nor measured modal BRIR.
6. On the first intentional switch from an *untouched legacy factory scene* to P9, an isolated native-ACKed close music stage initializes: 82° source spread, 1.6m, Large, 78% ER, 33% FDN, 0 dB output, orbit OFF. Existing .smix and manually adjusted states do not reset. No new state version or GUI visual redesign.

## Objective PCM admission

Tests use real native C++ at both 44.1 and 48 kHz. Check full source-referenced 70/116Hz+5.2kHz steady-state input/output RMS at output gain 0dB, loudness floor/ceiling, finite peaks below the same 0.98 linked guard, 128/256 block bit parity, left/right/anti-phase, 0% ROOM modal bit-exact zero, nonzero room response, and head-yaw changes. An additional isolated *70Hz-only* WorldBass test verifies left/right ear dominance reverses when the listener rotates, so treble cannot camouflage headlocked bass.

Reference PCM result after gain correction (source sum RMS 0.40890):
- 44.1k P9 sum RMS 0.41656, approximately +0.16 dB; P8.9 0.37159.
- 48k P9 sum RMS 0.41477, approximately +0.12 dB; P8.9 0.37152.

These are NOT music integrated LUFS, not physical headphone acoustic dB, and not proof of perceptual externalization. The new isolated 70Hz rotation test and Windows JUCE CTest must pass for its own exact commit, not inherited from an earlier source run.

## Required headphone FAT

- Compare raw stereo versus P8.2, P8.9, reconciled P9.1/P9.2 and dearVR PRO 2 under matched output LUFS within +/-0.5 LU. Disable all other FX and parallel dearVR insert while testing each.
- Material: percussion kick, bass guitar/sub 30–120Hz, orchestral ensemble, centered vocal, sibilance/transients, anti-phase stereo torture and real recordings with substantial 60–180Hz.
- Positions: 82°/120°/180° speaker spread, 1.6/3/6.7m, yaw -90/0/90/180. Toggle Large/Chamber/OFF with same level; distinguish bass *impact* from low-frequency bass *boom* and transient masking.
- Analysis: frequency response, LF group delay and modal decay, true IACC/ILD/ITD, DRR, headphone correction, CPU P95 per callback, silence/denormal/NaN and resumed device handling. Existing +7dB user A/B can be monitored but must NOT become default gain.
- Record scene, capture screenshots, compare with dearVR by listening. Do not claim measured dearVR feature parity or commercial release before FAT.

## Engineering constraints

No heap, locks, SIMD prerequisites, FFT allocation, filesystem calls or extra callback inside PCM. Coefficients are prepared once, weights small fixed arrays, single native route. Room modes are low-level bounded returns, not uncontrolled feedback. Preserve previous saved session schemas v1..v11, global FX gate, MAIN continuity and ASIO priority. Production main and P8.2–P8.9 remain unmodified.
