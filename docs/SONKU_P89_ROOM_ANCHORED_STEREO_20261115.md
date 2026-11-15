# SonKu P8.9 — Room-Anchored Stereo, Owner Headphone FAT
Private research, 2026-11-15. No merge to main, no public release.

## Why: source-derived cause, not invented sound preset
At 7.3m, SPACE LARGE 28.8m, P8.7 user reports in-head/up/too close even with elevation -7.4°, ER100, reverb100. P8.8 correctly separates physical XYZ elevation from measured generic KEMAR spectral-HRTF trim and makes distant HRTF/high direct and original LF quieter. However, **the full high-passed stereo SIDE is returned directly in headphone left/right, unscaled by source distance** (SonKuSpatialCore::process sideRescue, at all yaw/azimuth). This preserves anti-phase music and was necessary to avoid the rejected P8.3 regression, but is a strong nonspatial near-head reference. Room reflections already have measured directional KEMAR HRIR and world/head yaw transforms, so it is preferable to reallocate a bounded small part of far-field emphasis toward existing binaural horizontal early reflections, not saturate the late reverb or invent ITD.

Sources outside repo:
- Best et al., 'Sound Externalization: A Review of Recent Research' (2020), https://pmc.ncbi.nlm.nih.gov/articles/PMC7488874/ — binaural differences in reflections and fluctuations in interaural coherence crucial; simply diotic reverb is insufficient; generic KEMAR cannot guarantee subjective externalization.
- Choe and Choi, JASA 2023, DOI 10.1121/10.0023095 — DRR and IACC contributions interact with head motion/source direction.
- 'Enhancing Externalization of Frontal Virtual Sound Sources ...' (2026), https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/ — reflections 20-80ms and binaural differences frequently matter more than late reverb >80-100ms.
- AES 2021 Jot et al., https://audio.rightmark.org/lukin/pub/aes_extern/ — preserving music fidelity with minimal changes is essential for stereo post-processing.

## Isolated P8.9 mode8, actual native callbacks
- P8.2=0, P8.4=3, P8.5=4, P8.6=5, P8.7=6, P8.8=7 remain unchanged. Owner-rejected P8.3 1/2 remain blocked. P8.9 opt-in **mode8**, no automatic preset selection, no new audio engine, no extra HRIR FIR, no heap/locks/threads/device changes.
- Native stereo high-frequency SIDE is scaled by `1 - 0.22*d` only in mode8 + ROOM ON, where `d=clamp((distance-1.6)/6.4,0,1)`. Thus **minimum 78% SIDE** at maximum distance, **100% at close1.6m and ROOM OFF**. Stereo anti-phase stays audible; no mid-only collapse or surprise mono.
- Already-built **six-wall measured binaural early reflections**, horizontal first 4 and subdued floor+ceiling, receive a stage-only factor `1 + .24*d` atop P8.6's existing horizontal weighting. Fixed geometry, image timing, head yaw and source placement remain. At user7.3m, d≈.89 => 1.214.
- Diffuse existing stereo FDN tail receives `1 - .08*d` to avoid reverb100% becoming washy. At user7.3m factor≈.929. The actual FDN spectra, room60/early KEMAR and P8.7 LF warmth/air all untouched.
- The scalar direct HRTF and LF distance laws of P8.8 stay. HRTF trim separately controls perceived height without changing physical XYZ or floor/ceiling path. No psychoacoustic guarantee, individual HRTF/headphone response remains unknown.
- SIDE/room coefficients precomputed once per native PCM block; scalars only per sample. No new 140-tap convolution.
- Native typed bridge schema **v10**; **63-byte state layout identical to v9**. v9 restores P8.8 mode7/trim, v8 P8.7 mode6, v7 P8.6, v6 P8.5, v5 P8.4, v1..4 safe migration. Explicit P8.9 capability flag only when exact native JUCE KEMAR profile ready. GUI option appears only after v10 source readback and accepted ACK.

## Required software and headphone acceptance
- Source PCM at 44.1k and 48k, 64/128: ROOM OFF mode8 **bit-identical** P8.8 mode7 at azimuth24, head yaw0/60/180, correlated and anti-phase; ROOM ON near1.6m also bit-identical, including reverb100. Dry mix 0 is bit-identical; no clip/silence.
- FAR7.3m large room: all-channel music, anti-phase and correlated must remain finite and audible, stereo SIDE preserved and measured early reflected energy increased relative to P8.8; late FDN not amplified.
- Real JUCE native v10 tests native SPSC audio-applied ACK, v10 63-byte save and reopen, old v9 P8.8 preserve mode7/trim without downgrade, v1..8 session migration. Windows x64 full GUI exe, exact embedded GUI resources, standalone ZIP and SHA256 only if all scoped CTests pass.
- **Owner headphone FAT**: choose STATIC, head yaw0°, speaker angle60°, SOURCE ELEVATION=0° physical. P8.8 vs P8.9 matched output gain: ROOM OFF (must sound identical); SMALL/LARGE with distance1.6 then 3 then 7.3m, ER55 then100, reverb47 then100, HRTF ear calibration same between modes. Judge whether guitar/transient/vocal detail and stereo width remain musical while the stage separates from ears. Then test yaw±60/90/180, motion and realtime device reopen. Do not compare with unlevel-matched loudness.
- Do not claim that laboratory test establishes user perceptual immersive sound; generic HRTF, headphone EQ and actual head-motion still need FAT. **Do not merge stacked research ancestry (includes dormant rejected P8.3 code) to main.** No external device monitor toggles and no auto mute/master edits; audio continuity PR713 remains separate.
