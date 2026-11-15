# 8D-SonKu P8.5 — world/head-coherent music anchor (research only)

## Field feedback and causal audit

The owner's Windows P8.4 headphone FAT (`641b4eae`, Actions `43653357348`) rated P8.4 slightly more musical than P8.2 but reported inadequate externalization, with the apparent source in front and above the head. P8.3 was *rejected* due to severe musical collapse; neither P8.3 nor its LF480 matrix is permitted as a native selectable mode in P8.5.

In actual P8.4 `SonKuSpatialCore::updateKernels`, the common music MID anchor used `interpolate(centerTarget_, degrees_)`. Meanwhile its left/right virtual speaker HRIR used `p83::stereo(degrees_, spread, headYaw)`, and its early reflection paths used headYaw. This was a **verified reference-frame mismatch**, not proof that elevation or ceiling reflections are the perceptual cause. Strong head-locked SIDE, late FDN directionlessness, generic MIT KEMAR HRTF and actual headphone response remain competing factors. The owner FAT at yaw180/spread180/28.8m/reflections100% is not a neutral diagnostic.

## Source implementation

- `ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT` and `SONKU_P85_WORLD_COHERENCE_RESEARCH`, both default OFF in production.
- Mode 4 corrects **only** the P8.4 common MID anchor to `p83::toHead(worldAzimuth,headYaw)`; the two speaker HRIR and early reflections already use world/head separation.
- One authoritative native JUCE `SonKuSpatialCore`. The identical P8.4 fused FIR, bass crossover, music SIDE preservation, tonal correction, reflection gains and reverb remain active. No second engine, no unlicensed BRIR, no extra per-sample FIR, no dynamic allocation in the callback.
- Modes 0=P8.2, 3=P8.4 and 4=P8.5 are selectable in exact native schema-v6 capability-gated UI. Native state v6 remains the existing 59-byte layout; v1–v5 restore supported with explicit old P8.3 mode migration to 0. Real audio-side mode switch still uses 12ms fade-out/fade-in and ACK/readback, not browser-only labels.
- In v6 research UI, changing mode does not overwrite scene yaw, azimuth, distance, ROOM or motion parameters. Production/offline P8.2 and P8.4 processing code is unchanged.
- A v6 research branch is still stacked on the older P8.4 *research* code, which includes P8.3-era dormant implementation. **Do not merge this branch to main or publish until a clean P8.2-based extraction after physical FAT.**

## Non-negotiable waveform gates

Native source tests require: P8.4 and P8.5 **bit-identical** on neutral yaw0 (ROOM OFF/ON and correlated/anti-phase); block 64/128 parity; full direct PCM at world180+yaw180 in P8.5 identical to world0+yaw0 in P8.4 (ROOM OFF); and distinguishable erroneous P8.4 center transfer for the same world180+yaw180 test. JUCE v6 must separately confirm mode-4 SPSC queued audio-applied ACK, state v6, v1–v5 restoration and invalid old modes. The Windows field workflow verifies executable, embedded GUI files, full source SHA and portable ZIP.

These gates can verify PCM geometry without establishing that headphones produce out-of-head localization. Do not relax checks after failure.

## Headphone FAT

Use identical song/headphones/Windows level, disable downstream spatial enhancements, and level-match the actual recorded output. First compare yaw0, 60° stereo, STATIC, 1.6–3m with ROOM OFF, then ROOM ON. **P8.4 and P8.5 yaw0 are intentionally identical**; improvement should not be claimed in that case.

Then independently try yaw ±30/±60/±90/180 and angle 60°/180°, with FRONT, LEFT, RIGHT and REAR world positions, followed by the owner LARGE/100%-reflections case. Score bass, vocal openness, transient, fatigue, front/back and perceived elevation, source distance and stereo focus separately. Choose P8.2/P8.4 immediately if P8.5 sounds worse. True wide-angle world-locked SIDE and listener/headphone personalization are future separately measured hypotheses; **do not solve them using arbitrary crossfeed or aggressive side removal**.

References (research only, no proprietary assets copied): [Sennheiser dearVR Clarity](https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2), [SOFA / AES69](https://www.sofacoustics.org/), [2025 sound externalization and non-individual HRTFs](https://pubmed.ncbi.nlm.nih.gov/40748294/).

## Release veto

No claimed dearVR parity, no public release or merge, no physical ASIO/XRUN qualification, and no assertion of real externalization without the owner's headphone FAT. P8.4 continues as the current perceptual golden and P8.2 as fallback.
