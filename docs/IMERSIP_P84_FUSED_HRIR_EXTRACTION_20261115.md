# ImerSip experimental fused HRIR extraction (2026-11-15)

## Why
The user listened to the SonKu P8.4 screen / executable `v0.5.0-dev+g641b4eae` and reports it sounds BETTER than P8.2. Listening preference supersedes PR #704's older text saying P8.4 is unapproved; formal hardware FAT measurements and externalization proof still remain outstanding.

P8.4 reference screenshot: render mode `SPATIAL · P8.4 MUSIC FIRST`; virtual speaker angle **180°**, head-yaw slider **180°**, spatial mix **100%**, output **+2.5 dB**, LARGE 28.8 × 28.8 m, distance **6.1 m**, reflections **100%**, reverb **47%**. Clockwise button appears highlighted; screenshot cannot establish whether transport/audio motion is occurring. These are user-shared visual control settings, not measured PCM.

## Isolation: do NOT merge rejected P8.3
Original tested fusion kernel: PR #704 commit `641b4eaec653b2ecacc997200356a53aa98c4339`, `engine/builtin_fx/sonku/SonKuP84FusedMusicHrir.h`, copied unchanged into the P8.2-based ImerSip PR #706. This extracts **just** high-pass coefficient-space directional-contrast fusion; NO P8.3 physical/LF480/dual-callback code, no PR #704 parent branch history.

For each measured L/R speaker source, ear and FIR tap:

`H_ImerSip = H_center + 0.12 × HP750(H_measured_direction - H_center)`.

The bounded 140-tap kernel calculation occurs during the existing 16-frame direct-kernel target update. It operates in the existing single SonKuSpatialCore and existing four direct FIRs. It is enabled **only if the ImerSip research adapter explicitly calls** `enableImerSipMeasuredStereoSide(true)`; the ordinary SonKu P8.2 center path remains untouched. Crucially, ImerSip then discards the spatialized MID and preserves the ORIGINAL source MID for direct vocals. It retains the original LF, phase-aligned SIDE, room differential and optional gentle detail. No audio callback allocations, second engine, power normalization or source motion.

Note: this is **not an exact copy of the full SonKu P8.4 monitor**: ImerSip still has a different dry-MID level, controlled room-return balance and fixed-front scene. It is a deliberate, bounded transfer of the successful idea, not a fabricated claim of P8.4 sound equivalence or superior externalization.

## Validation
- Source `tests/imersip_music_phase_guard_test.cpp`: actual P8.2 source and transplanted P8.4 kernel, 44.1/48k, kernel differential and nearly unity summed LF/DC, exact ordinary SonKu OFF-mode parity, actual KEMAR L/R impulse difference, preserved raw vocal MID/stereo SIDE, bass guard, detail HF/1–3 kHz, 64/128/8192 chunk parity and sample-peak headroom.
- Windows `ARMixImerSipNativeTests`: actual JUCE editor instantiation, FX catalog, audio-applied SPSC settings, IMRS v1→v2 session migration and accepted state tests. Full Windows portable artifact is only valid after this gate and provenance checks pass.
- These are *necessary engineering gates*, not audible speaker externalization proof or calibrated 4× true-peak/BS.1770-5/ASIO XRUN evidence.

## Physical audition against the owner's proven sound
A: only SonKu P8.4 with the screenshot settings. B: only new ImerSip; start Concert Hall, Stage Width 95% (175°), Immersion 58%, Space 45%, Bass 75%, Detail 40%, Output volume matched. Bypass dearVR and the other spatial plugin in each case; stacking them will invalidate sound and phase observations. Loudness-match the A/B (P8.4 screenshot shows +2.5 dB), then evaluate actual vocals 1–4 kHz, 25–220 Hz bass pressure, frontal stage, stereo percussion separation, image externalization and natural late room. Revert/veto this extracted fusion if B sounds smaller, muffled, overbright, center-collapsed, or less musical than A. No modification to SonKu golden or public main is authorized by synthetic PASS alone.
