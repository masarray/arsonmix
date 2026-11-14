# Harmonice GrandStage — P0 native concert/cinema audition (2026-11-15)

## Product intent
GrandStage is a separate **stereo music / fixed-front venue processor**, not the 8D-SonKu moving-source spatializer. Target: a luxurious, open, live premium-system sound with clear/front vocals, large but believable concert/cinema ambience and intact bass/side energy. There is no orbit, automatic rotation, user-ambiguous head movement or secret gain boosting.

## Research baseline and isolation
- Based on the **actual SonKu P8.2** source in PR #695, not today's production `main` SonKu, and NEVER P8.3 (#701), which failed owner headphone listening for being quiet, hollow and boxed-in.
- Uses `SonKuSpatialCore::prepare(44.1/48k, SixSurfaceKemarResearch)` with P7.5 music MID/SIDE, P8 transparent LF and P8.2 6-wall / 3-band decay compiled together.
- Source branch is based on P8.2; changes are **not merged into main**, do not patch existing SonKu presets, do not alter a production callback, and do not claim public-release qualification.
- Catalog entry is compiled ONLY if `ARMIX_SONKU_EXPERIMENTAL_P82_ROOM=ON`. Therefore a default release cannot accidentally advertise an unsupported DSP feature. Existing P8.2 dependency gates also require `ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT=ON`, `ARMIX_SONKU_EXPERIMENTAL_MUSIC_SAFE=ON`, `ARMIX_SONKU_EXPERIMENTAL_P8_TRANSPARENT_BASS=ON`.
- One new native JUCE insert through the existing `NativeBuiltInFx` registration, graph/lease/bypass infrastructure and PCM callback. No VST packaging, WebAudio shadow mix, background worker, dynamic coefficient allocation, additional device graph or duplicate routing.

## P0 audible scene
P0 uses the P8.2 musical / room DSP; it does **not** claim a new proprietary BRIR, concert impulse response, cinematic bass management or a mastered EQ curve. It deliberately avoids inventing a 'bass boost' that could obscure fidelity failures. Controls are typed/serializable but *not exposed by a custom editor yet*.

| Field | Default | Design |
| --- | --- | --- |
| Venue | Concert Hall | Concert Hall / Premium Cinema / Live Room, with fixed room/distance choices |
| Immersion | 0.58 | P8.2 spatial bed blended at bounded 0..65%; zero is dry |
| Space | 0.45 | Coherent ER/FDN presence from the actual P8.2 wall materials |
| Width | 0.60 | P8.2 music-safe stereo SIDE; bounded, never rotation |
| Trim | 1.0 | Full-output linear trim (no auto level match or hidden AGC) |

- The set of parameters is prepared in a single scene transfer. DSP state is 28 bytes with unique `GRST` header/version 1; invalid partial/NaN/foreign state fails closed.
- Host control→PCM settings use SPSC; control ACK is allowed only after audio acceptance and a readback. Engine `getStateInformation` returns only accepted state.
- P0 only ships one default preset in picker. The native specialized editor and actual per-parameter UI/ACK binding are **next gates**; no unverified shiny GUI.

## Acceptance gates (not yet claimed PASS)
1. Compile and run actual P8.2 PCM with 44.1/48 kHz, exact dry, zero orbit, mono/hard-L/hard-R/anti-phase music, finite samples, bounded peaks and 64-frame parity. Fast job deliberately bypasses expensive installer and full native executable build.
2. Native JUCE registry/plugin-state roundtrip, PRE/POST routing, FX ON/OFF, session save/reopen/device restart and dual inserts; ensure no unsupported device rate.
3. Objective full-band LF/MID/HF tonal response, matched-gain A/B, stereo M/S / IACC / width, DRR and RT60, CPU p95/p99 on 1/2/4 instances, with strict vetoes on loss of musical weight. P0 only has a catastrophic-energy tripwire, **not** the final quality gate.
4. Owner A/B on headphones and actual physical playback, normalized loudness, particularly vocal front anchoring, bass body, mid openness, large-room realism and fatigue. Do not announce concert/dearVR-equivalent sound based on CI.

## Path to production
After source and physical FAT, port P8.2 algorithm *as a separate shared, versioned native DSP module* onto current `main` without overwriting newer GUI or SonKu defaults. Add minimal elegant GrandStage editor with Concert/Cinema/Live scenes and Immersion, Space, Width, Output; wire real audio-ACK snapshots and GUI parity. Test the exact Windows executable and publish a labelled candidate artifact only after it compiles and owner can audition it.
