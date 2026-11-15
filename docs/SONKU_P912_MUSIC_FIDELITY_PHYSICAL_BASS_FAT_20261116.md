# SonKu P9.1+P9.2 — Music Fidelity and Physical Bass Foundation
**Research only. Headphone field acceptance required. 2026-11-16.**

## Reconciliation / baseline

- `main` HEAD `37985f02caecd93b22f853183888d81e18dc6736` includes audio I/O recovery (#713) and the approved mixer GUI. No attempt to merge/revert it here.
- SonKu P9.0 #717 HEAD `cd85ffee054dad6b3b752b5351d34921ddc92717` is a draft, based on P8.9 #716, with stable dropdown and measured 2×2 stereo FIR. This P9.1+P9.2 source branch descends from that exact SHA; it must remain draft and must not be merged straight to production.
- Comparison found P9 draft **257 commits ahead and four commits behind** `main`; `src/Main.cpp`, `CMakeLists.txt`, `p1-windows.yml`, and GUI changes require manually reviewed integration with #713 on a separate merge candidate. P9.2's FAT does not prove hardware audio continuity.
- P8.2–P9.0 native renders, UI option names, and old SONK state versions remain A/B references. The P9.2 candidate is **new opt-in renderMode=10**, schema and SONK state version 12, preserving the 63-byte v9–v12 payload structure. v1–v11 remain readable; old files must not silently select mode 10.

## Root causes found in actual P9 source

1. Old default `outputTrim=.75` (-2.50 dB) and direct speaker sum factor `sceneBinauralLevel=.5`. P9.0's LF headphone return and HRTF spectral response make overall loudness dependent on programme and direction. Owner needed +7dB to audition. Pure output gain is not a safe automatic fix; it also drives the existing peak limiter.
2. P9.0 `transparentBass` subtracts the <220Hz measured binaural ear transfer, then adds original channel-locked low-pass left/right. This defeats world-anchored LF source movement and can make bass feel disconnected from the spatialized harmonics.
3. The six wall paths and 8-line 3-band FDN do not model source-dependent axial bass room modes. Raising wet percentages cannot manufacture physical bass or prove dearVR parity.

## Implementation

- **P9.1 measured music foundation:** in `SonKuP92MusicBassFoundation.h`, at `prepare()` calculate broadband magnitude of actual bundled MIT KEMAR HRIR at 250/600/1200/2500Hz and 0/30/45° over both ears (no proprietary dataset). The stable source gain is `clamp(1/(sqrt(1/2)*meanMagnitude),1,1.9)`. Source outputs still pass through the old measured two-speaker FIR and head-relative HRTF, never independent AGC. We do **not** call this loudness matching; a LUFS test on music is a separate FAT gate.
- **P9.2 directional full-band bass:** remove the *original unpositioned headphone LF return* only in mode 10. Preserve the complete binaural low-pass contribution in the same 2×2 measured HRIR kernel so LF and 220Hz+ harmonics rotate with world speakers. Give the already-rendered ears a matched, broad +1.14x LF shelf near 105Hz. No oscillator, hard-band pan or independent low frequency speaker engine.
- **Prepared axial LF room modes:** three damped bandpass modes per room, with frequencies derived from `343*n/(2*dimension)` and even harmonics to avoid listener-centre pressure nulls. Each L/R virtual source excites its modes according to **world-space** position; excitation is injected into its source FIR history before the same 2×2 measured HRIR. Three Q7.5–10 modes, per-mode contribution bounded by `0.17/3`, 45ms slewed wet parameter; dimension/coefficients precomputed. This is a physically motivated **parametric approximation**, not a measured BRIR or subwoofer cabinet model.
- **Controlled room presence:** mode 10 uses its own lower direct/early/late ratio than the old P8.9 large affine wet amplification; the original reverb FDN remains authoritative. This avoids masking the direct speakers by boosting reverb simply to recover perceived loudness.
- **Music-first audition entry:** selecting P9.2 explicitly applies STATIC front pair, 82°, 1.6m, Large room, Reflections 72%, Reverb 38%, 100% Mix and 0dB trim. The normalized measured source gain replaces the need for pre-applied +7dB trim. **Never** modify original default initializers, existing saved sessions, or another render mode without an operator edit.

## Objective gates

- Native PCM: 44.1k/48k, source L-only/R-only/correlated MID/anti-phase/stereo, yaw -85/+85, reference P9.0 unchanged, room ON/OFF and 64/128/256 callback invariance, finite peak under sample-peak limiter.
- Native JUCE: actual `processBlock` ACK and readback, no speculative JS success, stable 63-byte schema-12 `.smix`, v1..v11 restore, v11 P9.0 mode 9 retained, old physical P8.3 prohibited, device reprepare.
- UI: mock 40 native polling cycles with combined P9.2/P9/P8.9 capability flags, no option repaint when dropdown is open; first-entry explicit preset only.
- Release: package verified Windows GUI EXE at exact source SHA with license and this guide. No public release or main merge.
- **Missing:** actual music loudness match ±0.5 LU integrated (dry/P9/P9.2/dearVR), bass frequency/decay/distortion vs controlled reference, headphone/externalization FAT, physical ASIO/restart and speaker/HRTF-specific tonal measurements. Numeric CTest cannot establish subjective parity.

## Headphone field evaluation

Use the same reference stereo mix with FX rack **SonKu ONLY** (dearVR bypassed while auditioning SonKu), headphones and matched LUFS. Compare dry, P8.2, P9.0, P9.2, and a separately configured dearVR PRO 2. Headphone sound pressure must be controlled during testing, especially kick transients. Start P9.2 preset at 82° front / 1.6m with orbit OFF; test yaw -90, 0, +90, rotation and distance 1.6/3/6.7m. Compare 40–160Hz punch, resonant tail, separation, full-range timbre, center vocal, measured/output clipping, and the physical *outside-head* sensation. Avoid attributing body vibration from real subwoofers to headphone DSP.

Reference: AES69 SOFA data interoperability https://www.sofacoustics.org/ ; externalization uses direct/reverberant cues and directional binaural room reflections, see https://www.mdpi.com/2076-3417/7/5/532 . The SonKu modal transfer here is **not** a SOFA/BRIR measurement.

## Failsafe

Do not change the approved mixer/routing, device recovery, GUI layout, user-owned `.smix` file or default P9/P8 programmes. Keep the branch draft if any native CTest, real Windows packaging, UI repeatability, LF tone, or headphone fatigue test fails.
