# 8D-SonKu → externalized, music-faithful binaural audio
2026-11-13 · Engineering audit + P0 music integrity implementation · EXPERIMENTAL (not a public release)

## Objective and provenance

Owner reports **dearVR externalizes music convincingly, with depth and tonality preserved**, while SonKu sounds near/in-head and loses parts of the stereo music. This is subjective field evidence; the two renders have not yet been loudness matched or evaluated with the same stems, headphones and HRTF personalization.

**DO NOT copy proprietary dearVR CORE/AMBEO Clarity code, coefficients, internal IR/BRIR, or product UI.** Refer to published techniques and build SonKu's own real-time algorithm. Sennheiser describes patented AMBEO Clarity as a tradeoff between HRTF timbre coloration and localization/externalization. This repository contains original C++ and licensed MIT KEMAR measurements, not dearVR files.

Primary published references:
- Official dearVR PRO 2 manual (January 2024), Position, Reflections, Reverb and Master/Clarity sections: https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf
- Sennheiser engineering article (2025), Clarity and musical timbre preservation: https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2
- Sennheiser PRO 2 release, stereo width, controllable distance and separate early/late acoustic filters: https://newsroom.sennheiser.com/dear-reality-releases-dearvr-pro-2-spatializer-plugin-xhdtie
- 2024 review, reverberation, listener movement and individualized HRTFs for externalization: https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2024.1408073/full
- AES Convention 157 (2024), timbral vs spatial fidelity in different headphone renderers: https://aes.org/publications/elibrary/elibrary-page/?id=22754
- MIT KEMAR HRIR provenance: docs/THIRD_PARTY_8D_SONKU_HRTF.md

## Actual code-level gap in v0.5.0-alpha.23

The **single authoritative JUCE PCM graph** calls `SonKuSpatialProcessor::processBlock` → `SonKuSpatialCore::process`. The renderer uses **two** directional measured KEMAR HRIR FIR sources and, for each ear, sums
`y = 0.5 * gain * (H0 * inputL + H1 * inputR)`.
With `stereoSpreadDegrees=0`, `H0 == H1`; for `inputR = -inputL`, the direct field cancels **exactly**. Room's diffuse return, if enabled, cannot restore an already missing dry stereo side. At nonzero spread, low-frequency HRIR similarity can still produce partial cancellation. This is an actual deterministic DSP failure, independent of PNG, WebView2 and virtual orbit cosmetics.

Additional limitations: horizontal-only non-individualized 0° elevation KEMAR; 2D first-order four-wall ER with light ITD/ILD proxy instead of true directional BRIR; synthetic eight-line late room; basic monitor gain/bass compensation. There is no headphone-specific equalization, individual ear selection, head tracker or frequency-dependent HRTF clarity control. Avoid marketing these as implemented or claiming parity.

## P0 implemented in PR (candidate only): phase-safe stereo music integrity

- `SonKuStereoIntegrity` owns fixed per-processor correlation state; 20ms cross/energy detector, -0.78 engage / -0.55 disengage hysteresis, 15ms gain smoothing, silence release and bounded finite evidence. No allocation, extra render thread, FFT, new device callback, WIRE or routing authority; no added reported sample latency.
- Preserve the source side difference `0.5 * (L-R)` **after** the binaural FIR summation, independently and symmetrically in the two output ears; never fold that side signal to mono. Bounded .65 × native mix × gain and only strong negative phase correlation. This is a protective **side-bed**; it is not itself an externalized HRTF image, and will not necessarily survive a mono L+R fold-down (nor does a native anti-phase stereo recording).
- Ordinary coherent L=R center remains numerically equivalent to the prior KEMAR impulse; `spatialMix=0` is unchanged. Native ROOM OFF, early-reflection/distance A/B and native ROOM ON share the same guard. No plugin/preset identity, SONK v1/v2 state, session ABI, transport, FX bypass or UI changes.
- Exact-source C++ test at 44.1/48 kHz covers L=R impulse equality to published KEMAR taps; anti-phase stereo FRONT/RIGHT/REAR/LEFT minimum ear RMS, bounded peak, sample partition parity; normal positive stereo no-op, silent release, reset and dry mix identity.
- Fast A/B renderer writes 4s equal-input-level original generated tone stems: coherent FRONT, anti-phase FRONT and anti-phase REAR. **Headphones only.** No normalization, synthetic copyright issue or dearVR name use in artifacts.
- The audio developer must not merge to main solely on these mathematical tests. Human listening can reveal audible color, internalized side bed or changes to stereo image; listen to real music.

## Next quality milestones (requires objective + listening evidence)

1. **P1 spectral/music fidelity.** Capture ear transfer functions FRONT/45/90/135/REAR and L/R & mid/side energy in 1/3-octave bands, compare dry stereo vs wet binaural at matched level, isolate bass loss, comb notches and late-reverb masking. Develop a *new SonKu* prepared minimum-phase spectral compensation or complementary dry/timbre-control stage, with strict frequency-band and headroom limits and a user-facing quality mode. Verify it improves music without defeating HRIR ITD/ILD. Never extrapolate from one KEMAR listener.
2. **P2 externalization and geometry.** Introduce licensed multi-elevation individualized/selectable SOFA HRIR datasets (per-data license check), reconstruct 3D directional image-source reflections including floor/ceiling; optionally measured BRIR with separate direct/ER/late paths. Control direct-to-reverberant ratio, air absorption and listener distance independently. Prepare all FFT/filter coefficients out of the audio callback. Avoid naive reverb amount as a proxy for distance.
3. **P3 motion/stability.** Evaluate optional head tracking and frequency-dependent directional interpolation; measure adaptation latency, glitch-free seek, 44.1/48k and 64/128/256 frame callback CPU p95/p99, memory, zero allocation and XRUNs in Windows ASIO (real hardware).
4. **Perceptual acceptance.** Compare the *same* licensed mono voice, recorded stereo music and anti-phase/wide stems through dearVR PRO 2 and SonKu using the same actual headphones, disabled OS spatial enhancements and loudness matched within 0.2 LUFS. Blind A/B rate front/back/externalization, distance ordering, musical tone integrity, stereo width, artifacts and listener preference. Capture reference plugin preset, sample rate, distance, stereo width, gain and room settings. A single engineer cannot prove all-listener equivalence.

## Release gate
This PR changes the **native audio** of a user-installed FX, not just the picture. Do not mark production/dearVR-equivalent, edit README as shipped, or trigger a public Windows installer until source tests, offline A/B, performance telemetry, real Windows WebView2/ASIO/headphone listening and user approval are complete.
