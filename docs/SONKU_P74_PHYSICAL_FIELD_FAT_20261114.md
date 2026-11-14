# 8D-SonKu P7.4 — physical Windows ASIO / headphone field qualification

Status: **experimental FAT candidate, NOT a public release**. No dearVR-equivalence claim. The native XYZ feature is OFF in default/public CMake configurations; this field binary explicitly turns `ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT=ON`. Candidate is intended only for controlled listening and compatibility tests.

## Provenance and acceptance sequence

1. Use only the **ArSonMix-SonKu-P74-XYZ-Windows-x64** GitHub Actions artifact produced by `.github/workflows/sonku-p74-windows-field.yml`. Do not use a public alpha installer to test P7.4. The run must show **preflight PASS**, **full Windows ARMixConsole compile PASS**, **three JUCE CTests PASS**, **GUI PE/resources verification PASS**, and **artifact upload**.
2. Extract the artifact into a fresh writable directory, preserving `portable_mode.txt`. Confirm `BUILD_PROVENANCE.json` has the expected source commit, `sonkuExperiment.enabled=true`, `stateSchema=3`, `physicalAsioQualified=false`, and the included MIT KEMAR `THIRD_PARTY_8D_SONKU_HRTF.md` notice. Never substitute a test-console executable.
3. Use low monitor level at first, headphones connected to the correct physical audio interface outputs, and no live audience/performer-critical audio. Microsoft Edge WebView2 Runtime and appropriate ASIO driver must already be available.
4. Open `ArSonMix-GUI10CH-P012-Windows-x64.exe`; choose the verified ASIO device in Audio Setup. Use a clean session, not an irreplaceable Show file. Source (hardware input or legal local stereo test music) → CH → built-in **8D-SonKu** → WIRE → MAIN → physical stereo headphone outputs.
5. In the **existing** SonKu native FX editor, request/verify a current v3 snapshot. Only a matching host/ChannelId/instance/deviceEpoch/graphRevision readback may expose **SOURCE ELEVATION**. If hidden, stop: capture exact SHA, UI screenshot and diagnostics; do not assume browser motion is applied PCM.
6. Test one bounded edit at a time: azimuth FRONT/RIGHT/REAR/LEFT, 0° elevation then above/below, 1.6–10 m available source distance within room geometry, ROOM OFF/SQUARE/CHAMBER/LARGE. Confirm queued request, AUDIO ACK, appliedRequestId and matching accepted snapshot. Invalid height/distance must reject or fit according to the documented bridge contract, not falsely ACK.
7. Press the single topbar **Save** to a new `.smix`; close/reopen with **Open**. Check actual accepted XYZ/room values, audible path, WIRE, FX order and state. Repeat with 44.1/48 kHz, device stop/start and graph revision changes. Old v1/v2 FX/session state should restore elevation to 0°, never stale XYZ. Save source file backups separately.
8. Run 1, 2 and 4 SonKu inserts plus at least one normal VST3 and WIRE routing. Repeat at 44.1/48 kHz and 64/128/256 sample buffers where the ASIO driver supports them. Record device model, driver version, sample rate, frame size, inserts, CPU/p95/p99 if available, latency, XRUN/dropout counts and faults. Never claim real-time performance from hosted Linux benchmarks.
9. For musical integrity, compare original source, SonKu LIGHT/public, this experimental P7.4, and **dearVR PRO 2** at equivalent output levels (target matched within ±0.5 dB using measured RMS/LUFS). Include vocal, bass, dense mix, natural transients and intentionally anti-phase / correlated stereo samples. Assess tonal damage, phase cancellation, front/back discrimination, elevation, room depth, head-locked sound, glitches and gain; evaluate with at least one independent headphone listener. A/B listening must be blind to variant where practical.
10. Stop the experiment immediately on persistent loud bursts, severe cancellation, device crashes, dropouts or audible processing clicks. Export diagnostics and record the minimal reproduction before adjusting DSP. Do not release or merge to public `main` on a synthetic test pass alone.

## Case record template

| Case | Conditions | Observed | Outcome |
| --- | --- | --- | --- |
| Build/launch | Artifact run, SHA, Windows, ASIO device | EXE starts; true native v3 | PENDING |
| UI→DSP | FRONT/REAR, elevation, ROOM edits, ACK/snapshot | Values + audible response | PENDING |
| Persistence | Save/Open `.smix` + restart/rebind | Correct position and route | PENDING |
| Load | 1/2/4 instances × 44.1/48 kHz × 64/128/256 where supported | XRUN, p95/p99, audio | PENDING |
| Quality | Matched real music and dearVR, headphone blind A/B | Tonal, front/back, distance | PENDING |
| Licensing | MIT KEMAR measurement attribution and license in test ZIP | Notice readable | PENDING |

## Release constraints

The field artifact is for verification **only**: no auto-promotion, installer creation, GitHub Release, or change of public LIGHT/default. Once measured FAT results exist, a separate production decision must cover CPU headroom policy, licensing/provenance review, safe session upgrade and human headphone quality. Do not advertise “equal to dearVR” without reproducible evidence.

Physics/UX comparison: dearVR PRO 2 public manual (position, six-boundary reflections, reverb and output) https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf and Sennheiser Clarity documentation https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2 . These are **targets**, not copied algorithms or validated parity.

MIT KEMAR source/redistribution provenance: `docs/THIRD_PARTY_8D_SONKU_HRTF.md`. Treat notice inclusion as mandatory for sharing a field binary.
