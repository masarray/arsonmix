# SonKu P8.8 — Perceptual Ear Height Calibration and True Distance Cues
Private headphone FAT, 2026-11-15. **No merge to main, no public release.**

## Owner report (screenshot + ears)

P8.7, 28.8m LARGE, 7.3m source distance, **-7.4° physical elevation**, 100% early reflections, 100% reverb, yaw 0°, 24° speaker angle (orbit may be ON), 100% spatial, +2.5dB output: still near and consistently above ear/head. Moving physical elevation slightly down helped a bit, not enough. P8.6 speaker was more focused; P8.7 added air/bass; preserve both. Not a request to automatically reset controls.

## Exact source proof

1. Source geometry uses `sourceHeight = earHeight 1.45m + distance*sin(elevation)`. At 7.3m / -7.4°, **source height ≈0.51m, only 1cm above allowed room floor safety boundary**. More negative physical elevation is not physically possible in that room. The physical Z projection is correctly signed; native measured 3D HRIR interpolation is actually invoked when elevation is nonzero.
2. The KEMAR HRIR bank is **generic, not individualized**. Correct physical 0/-7.4° can be *perceived* as too high for a particular listener/headphone. Spectral peaks/notches from pinna and headphone transfer determine vertical perception, not ILD/ITD alone. Objective HRIR horizontal calibration cannot prove subjective height.
3. P8.7 deliberately uses `targetDirectDistance=1/(1+0.13*(r-1.6))` to preserve direct musical clarity. At 7.3m this gives ≈0.574 (~-4.8dB), while physical inverse-distance 1.6/7.3≈0.219 (~-13.2dB). The deliberately strong direct component can produce proximity despite distant UI setting. More importantly, original transparent stereo music LF is returned with **zero source-distance attenuation** even with ROOM ON. A distant high-band HRTF with an invariant headphone LF can feel near/in-head.
4. The room's horizontal four-wall, suppressed floor/ceiling and P8.7 direct air/FDN music-body are not broken by simply moving SOURCE ELEVATION. A 100% reverb user setting can mask direction, but automatically lowering it would rewrite the owner's scene and defeat A/B.

## External literature, not copied code

- Wang et al., Applied Acoustics (2025): spectral contrast and notches alter median-plane height localization, with up/down confusions in dynamic HRTF render: https://www.sciencedirect.com/science/article/pii/S0003682X24005759 .
- Auditory localization practical review (2024): generic HRTFs lead to elevation and externalization errors: https://pmc.ncbi.nlm.nih.gov/articles/PMC11267622/ .
- Hassager et al. pinna filtering: direct vs reflected spectral features and ear coherence matter for externalization: https://doi.org/10.1016/j.apacoust.2020.107257 .
- 2026 frontal BRIR study: DRR, spatial reflection level/direction and head rotation together contribute to externalization, not overall wet level alone: https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/ .

## P8.8 experiment — explicit opt-in mode 7

- Separate **SOURCE ELEVATION (physical XYZ)** from **EAR HEIGHT ALIGN · HRTF (perceptual KEMAR spectral lookup)**. The new trim is -30° .. +20° with default 0° and a combined measured lookup limited to -40° .. 60°. A trim of -12° at physical 0° gives the exact same direct KEMAR FIR as physical -12° with trim 0 in P8.7, **without lowering speaker geometry, floor reflection image, direct geometric distance or listener position**. This is user calibration, **not** an automatic guaranteed perceptual fix.
- Moderate P8.8-only source-distance law for direct high-band: `1/(1+0.27*(r-1.6))` = 0.394 at 7.3m (~-8.1 dB). Unlike P8.7 ~0.574, spatial direct is ~3.3 dB further back. No newly amplified six-wall ER, no diffuse reverb gain increase, no 1/r hard attenuation.
- Gentle P8.8-only distance effect on original stereo LF: `1/(1+0.10*(r-1.6))` = 0.637 at 7.3m, retaining musical low-body while removing old invariant head-locked LF near cue. The P8.7 existing FDN LF room-body and direct-air stay in place unchanged.
- Keep world/head yaw, MID/SIDE, fused two direct FIRs, four horizontal reflection pressures, floor 24%, ceiling 15%, ER 76%, tail 38%, P8.7 +1.9dB bounded direct air shelf, late LF body and all user controls. No extra FIR, audio callback, samples, heap or locks.
- **Mode 7, native schema v9**, readback/typed ACK. One new 4-byte float appended to old 59-byte state, total **63 bytes**. v1..v8 old sessions retain mode and do not acquire perceptual trim. The rejected P8.3 modes 1/2 remain blocked. The trim is inactive in legacy modes even if saved in a P8.8 session. Public default build flag `ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT` is OFF.

## Qualification

- Native core tests at 44.1/48k: P8.8 ROOM OFF trim0 exactly P8.7 at azimuth 0/90/180 and yaw 0/60/180 for correlated + anti-phase, 64/128 parity. P8.8 physical0 trim-12 has **bit-exact direct ROOM OFF PCM** versus P8.7 physical-12 trim0; physical source heights must differ.
- 7.3m LARGE no wet: P8.8 farther direct and LF pressure than P8.7, finite peak/output. User 100% ER/reverb wet tests at effective elevations -20,-10,0,+10, same 64/128.
- Real JUCE v9 test: native audio-applied ACK, 63-byte state and v8 P8.7 mode6 no-trim restoration; v7 P8.6 mode5, v6 P8.5 mode4, v5 P8.4 mode3; forbidden P8.3. Check GUI capability tied to native v9 and approved embedded Pages resources. Linux source tests **do not replace** native Windows/ASIO/headphone FAT.
- Listening: choose **STATIC not ORBIT** to establish direction, speaker angle ~60° and azimuth FRONT; SOURCE ELEVATION **0° (physical ear height)**, EAR HEIGHT ALIGN trim **0°, -5°, -10°, -15°, -20°** A/B. Set distance 1.6/3/7.3m. Compare ROOM OFF first (vertical HRTF) then LARGE reverb 25/47/100% (DRR and floor/ceiling impression). Then rotate yaw ±60/90/180 and world speaker azimuth. Compare P8.7 with P8.8 trim0 and matched loudness. If none of the generic KEMAR spectral positions reproduces ear-level localization, **do not keep blindly shifting downward**: next valid step is individualized HRTF/headphone EQ calibration or alternate public HRIR subjects.
- This is a **controlled headphone FAT candidate**. A more negative HRTF lookup does NOT guarantee perceptually lower sound for every user and may alter timbre or front/back accuracy. Do not call P8.8 a qualified fix, dearVR match or measured personal HRTF.
