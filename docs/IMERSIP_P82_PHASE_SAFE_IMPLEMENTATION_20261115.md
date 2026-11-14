# ImerSip — P8.2 phase-safe musical immersive sound (15 Nov 2026)

## User-approved goal
ImerSip is **not SonKu's rotating sound**. It is a fixed-front luxury Concert/Cinema music enhancement built on the **actual approved-at-headphones SonKu P8.2** music-safe transparent bass and six-wall/three-band room research from draft #695. The owner rejected P8.3 (#701) as dull, boxed, quiet and musically thinner; its stereo transfer is forbidden here.

P0 GrandStage prototype has been replaced/renamed **ImerSip**, identity `armix.builtin.imersip.v1`. No public production rollout; the research P8.2 branch is intentionally isolated from today's main/latest GUI.

## Signal flow / one owner
Stereo native insert (JUCE accepted channel FX lease, not WebAudio) ->
P8.2 **fixed FRONT** measured KEMAR, music-safe M/S, transparent original LF,
six-surface LF/MID/HF material + late FDN ->
ImerSip **single positive coherent low shelf per ear** ->
**positive high-pass SIDE shelf only, gated by true original-source SIDE** ->
preserve base P8.2 headroom (skip enhancement near 0 dBFS) ->
existing mixer channel routing. No movement. No duplicate engine, no worker,
no new device callback, no callback heap or trigonometric design.

### Explicit DSP invariants
- **Mid:** with bassImpact=0, `(Lout+Rout)/2 == (LP82+RP82)/2` to float arithmetic tolerance, **even on anti-phase input**. Added SIDE terms cancel exactly when folded to mono. With bassImpact>0, the mono MID is the same **coherent LF shelf** applied to P8.2 MID, never a delayed/opposite-polarity bass copy. Near peak safety can suppress optional boost, but it never attenuates base P8.2 samples.
- **Side:** one minimum-phase first-order HP `H_HP = 1 - LP_680Hz` and **non-negative** gain `k <= 0.20`; `H_SIDE = 1 + k H_HP` has no negative-real cancellation notch. A per-sample causal side detector derives gain from ORIGINAL input (mono center == 0 SIDE, anti-phase == maximum SIDE) without block-size-dependent decisions.
- **Bass:** `H_LF = 1 + b LP_120Hz`, `b >= 0`. The maximum intentional low-end bump is +0.70 dB concert, +0.85 dB cinema, +0.40 dB live room at immersion=bassImpact=1. The coherent filter is identical L/R, retains channel phase relation and does not introduce a separate bass time delay. A bounded, musically small enhancement—not multi-band loudness/exciter.
- **Loudness/peaks:** core P8.2 `outputTrim` remains an explicit control; no auto makeup gain, compressor, global limiter, loudness normalizer or automatic “rescue” of music. Peak protection caps *only* enhancement delta to available 0.98 FS sample headroom; **baseline P8.2 output is never reduced**. This is NOT 4× true-peak limiting; full device/headphone qualification still needed. Full-scale or clipped input cannot be guaranteed both louder and unchanged.
- **Dry:** `immersion=0, outputTrim=1` restores original stereo input within float tolerance; bass and SIDE enhancements both exactly zero.
- **RT CPU:** one P8.2 core only; fixed 8192 float side-gate scratch; no new FFT/convolution, heap, mutex, file IO, coefficient exp/pow or UI events on PCM callback. Causal per-sample envelopes and one-poles.

## Audio/user parameters, native state
| Control | Default | Meaning |
| --- | --- | --- |
| VENUE | Concert Hall | 0 concert / 1 cinema / 2 live room |
| IMMERSION | 0.58 | 0 dry, 1 maximum fixed-front P8.2 scene + controlled enhancement |
| SPACE | 0.45 | P8.2 original room material/FDN amount |
| WIDTH | 0.60 | nonnegative SIDE HF shelf, mono center untouched |
| BASS IMPACT | 0.75 | low-end positive coherent shelf, never a second delayed bass path |
| OUTPUT | 1.0 | linear, manual P8.2 output gain |

Stable new `IMRS` binary state version 1, 32 bytes. External `SONK` or previous experimental `GRST` states are invalid and fail closed. FX state is owned by existing mixer lease and **accepted DSP audio ACK** queue. The unique catalog ID cannot conflict with SonKu. There is **no yet-qualified custom JS editor** or parameter bridge; P0 native picker inserts the default Concert sound on a research build. Save/restore and direct in-process queue/ACK are exercised by dedicated JUCE test. Do not claim GUI editors are functional without bridge tests.

## Validation
- Native C++ PCM gate `tests/imersip_music_phase_guard_test.cpp` runs real P8.2 core at 44.1/48k. It checks **bit-level-close P8.2 parity** with BASS/WIDTH=0 (6 music fixtures x 3 venues), 64/128/8192 audio chunk invariance, exact MID for WIDTH-only, anti-phase stereo SIDE survival, bass mono 40/70/110/160/220 Hz **no drop**, headroom and combined enhancement RMS.
- Fast GitHub workflow `.github/workflows/imersip-fast.yml` compiles same P8.2 sources. It is a **source/PCM gate, not listening proof**.
- Native JUCE `tests/imersip_native_state_test.cpp` additionally checks SPSC, processed PCM, v1 state roundtrip/rejection. Compiled only when all four P8.2 research flags ON; avoids burdening public/default builds.
- Math-based continuous frequency simulation predicts positive, notch-free magnitude for both shelf transfers; this does not prove perceptual acoustic improvement, binaural localization, or bass fidelity with all mastered music. Need 4× true peak and ITU-R BS.1770-5/EBU R128 loudness A/B, LF spectrogram 25–200 Hz, M/S, interchannel correlation, sample+true peaks, 1/2/4 native insert p95/p99 CPU, anti-phase stress, and physical 44.1/48k headphones/ASIO listening FAT.

## Sources informing design (not proprietary code)
- AES paper: `https://www.aes.org/tmpFiles/elib/20250309/21939.pdf` (music externalization and coloration / filtering)
- Sennheiser dearVR PRO2 CLARITY notes: `https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2` (timbre/externalization tradeoff, not used as a copied algorithm)
- ITU-R BS.1770-5: `https://www.itu.int/rec/R-REC-BS.1770-5-202311-I` (loudness and true peak measurement)
- EBU Tech 3341: `https://tech.ebu.ch/publications/tech3341` (measurement/test signals)
- Source regression: #695 P8.2 baseline and #701 P8.3 hard field rejection.

## Remaining release gates
1. Exact native Windows JUCE research build and state SPSC test green (do not infer from Linux C++ gate).
2. Add an elegant compact ImerSip editor to `/preview/` and generated executable GUI, with **real native ACK/readback**, 6 controls, Save/Open and preset handling. Preview must be the exact executable source, never a separate WebAudio mock.
3. Match loudness and conduct blinded A/B using the OWNER's real music and Windows headphones (P8.2 as direct reference). Reject if bass loses pressure, vocals hollow, sides vanish or CPU is excessive.
4. After owner acceptance, **port isolated versioned shared P8.2 DSP onto latest main** without reverting newer GUI/device fixes. Build user-testable candidate installer only then.

> LIMITATION: A signal that is *already* perfectly 180° anti-phase will mathematically cancel on mono summation. No transparent stereo effect can both preserve every original channel exactly AND make its mono sum nonzero. ImerSip guarantees **no *additional* mono cancellation from its positive SIDE processing**, not the physically impossible elimination of all cancellation present in the input or baseline.
