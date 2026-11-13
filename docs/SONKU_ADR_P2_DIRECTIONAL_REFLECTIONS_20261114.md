# SonKu ADR P2 — Directional measured-HRIR image-source reflections
2026-11-14 · experimental on draft PR #674 · NOT public/released

## Verified source-level gap
Existing SonKuEarlyReflections uses measured wall image-source paths
but ear output is a delayed filtered mono sample with synthetic
0.31ms maximum ITD and 13% ILD proxy. It omits the pinna/FRONT-REAR
spectral signatures of each reflected arrival. The existing KEMAR
bank provides actual 0° elevation measured horizontal HRTF.

## Independent design
Retain exact current realtime default. Add OFFLINE-only
setDirectionalEarlyReflectionsHrir(true) forwarded to the original
room instance (not a parallel audio graph). Active changes are rejected.
For each of 2 stereo source emitters x 4 first-order image paths,
compute image-source arrival azimuth atan2(x,y) every 64 samples.
Use horizontal KEMAR 5° HRIR interpolation with ear mirroring and
existing wall/distance pressure law; apply one COMMON path delay
and measured left/right KEMAR 140-tap FIRs. The measured HRIR
already includes ITD/ILD: no additional fake ear delay/gain.
Smooth moving filter targets over 8ms, existing delay/gain slew
remains; fixed state and history, no allocation, I/O or locks in
callback. Baseline renderer path stays structurally unchanged.
No new GUI, .smix, VST3 catalog, routing or JUCE ACK protocol.

## Scientific and commercial provenance
dearVR PRO 2 *official user manual*, §§2.1 and 3.2:
https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf
describes first reflections and 6 surfaces; this is NOT a claim
about unpublished dearVR DSP. Steam Audio documentation:
https://partner.steamgames.com/doc/features/steam_audio
describes directional HRTF and reflected geometry, and associated
CPU costs. Meta spatial audio guide:
https://developers.meta.com/vr/design/spatial_audio/
explains spectral pinna cues in FRONT/REAR. SOFA/AES69:
https://www.sofacoustics.org/ for future multi-elevation BRIR
profile interchange. Existing SonKu KEMAR license remains under
docs/THIRD_PARTY_8D_SONKU_HRTF.md. No proprietary assets copied.

## Limitations and gates
This is a physically grounded HORIZONTAL early reflection pilot,
not full externalization, not 3D BRIR, not elevation or head tracking.
Four reflected 140-tap filters x two source emitters are expensive.
Do not publish the mode until CPU p99 on ASIO hardware, headphone
blinded same-gain A/B, front/rear clarity, timbre and DRR tests.
CI covers 44.1/48k causal delay, measured ear mirror, finite output,
legacy-default, rejection of active switching, off-silence, and
existing full source PCM/music integrity tests. Do not claim dearVR
quality merely from a synthetic regression or static source IR.

## P2 objective and CPU evidence additions
The same native offline renderer emits four 2s 48k/16bit WAVs:
FRONT/REAR x legacy light vs measured image-HRIR early reflections,
all on the identical original harmonic test input and exact direct
KEMAR. The Python evidence writer verifies direct PCM is byte-equal
before reflections, output finite/in-range, reflection difference
nonzero, and records directional-vs-light ear RMS and sample peaks.
These are **objective DSP differences**, not proof of perceived distance,
color or externalization. The informational runner benchmark compares
48k/128 3-second native wall-clock and callback p99 using the same
source and block size for light vs full measured reflections.
