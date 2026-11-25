# PRD — ArSon Harmony FX: Natural Backing Vocal Depth (v1.0)

<!-- HARMONICE_H1_OWNER_FEEDBACK_20261124 -->
## H1 — owner feedback: unity backing, natural Tune and live latency (2026-11-24)
**Status:** proposed corrective implementation contract; not shipped DSP or acoustic qualification. This amendment supersedes the earlier *always recessed backing* requirement at full Wet; preserve existing behavior at ordinary presets as closely as possible. Source audit: main `2253001926e6ef318eb00c9673417dc1177df4d6`. Related [ARD amendment](ARD_ARSON_HARMONY_DEPTH_ENGINE_20261109.md) and [H1 TRD / implementation and test strategy](TRD_HARMONICE_H1_LIVE_VOCAL_20261124.md).

- **P0 level:** Wet is an additive backing-level control, never a dry/wet crossfade. Wet=0 with Tune OFF and Gain=0 preserves the exact declared-latency L/R lead. At Wet=100, combined pitched backing stem should be comparable to the lead (target 0 dB, ±2 dB sustained voiced stereo RMS at Space=0/0.6/1, Color=1; ±3 dB on qualified real vowel phrases; loudness/listening evidence also required). Not unity per voice for 2–5 singers. Doubler has one unison companion and a distinct calibration. Wet intermediate values must be monotonic, preserve the current musical balance near factory Wet=.45 as far as feasible, and not conceal an automatic lead duck. Space remains width/distance/DRR and Color remains timbre; neither is a substitute for level. At lower Color, report perceptual loss rather than force a spectral AGC.
- **P0 Tune:** optional gentle lead-only pitch correction, OFF by default, independent of Wet/Color. Preserve natural vibrato, slides, consonants and stereo identity; avoid forced wrong-note snapping, zipper, flutter, metallic buzz or repeated dry/processed switching. A centered note must remain effectively dry. Weak/ambiguous pitch and unavailable synthesis must release smoothly; never introduce a hole or silence. Existing limited correction intent (max 34 cents, 9-cent deadband, ~80ms stable intent) is baseline, not a mandate to intensify correction. Music Guide supplies key/scale, not chord truth; no new feature or hidden mode.
- **P0 live comfort:** source currently delays lead by 2×16ms (~32ms; 1536 samples at 48k), even with Wet=0. This is not zero latency. H1 first fixes level/Tune without adding latency or changing graph PDC. H2 follows in the same roadmap with a separately benchmarked low-latency design: aim for zero added *dry monitoring* delay Tune OFF and ≤10ms added lead DSP Tune ON where feasible (stretch target, not guaranteed). Harmony may retain independent causal onset/synthesis delay; do not misrepresent whole-plugin latency or parallel phase alignment. Do not reduce quality merely to hit a number.
- **Headroom:** equal-level stems can add +3dB in average power or +6dB peak if coherent; keep float headroom, explicit output Gain and existing downstream overload controls. No hidden brickwall, AGC, compression or lead attenuation. Qualify overdrive and no extra clipping at specified nominal test levels; document user headroom guidance.
- **Compatibility:** one existing approved editor, catalog identity, AHM1 v1/v2/v3 state, presets, native ACK/readback, ChannelId Music Guide, continuous bypass and existing single render owner. No new GUI or preset schema required. Any semantic migration must be explicit and tested; preserve ordinary saved settings, compare full Wet changes intentionally.
- **Release:** source and CI are not acoustic or hardware proof. A/B real consenting vocal recordings and exact-binary ASIO monitoring measurements are required before calling the result natural or live-ready. Do not block unrelated INT-1 single-ASIO delivery for an unproven H2.

---

**Date:** 2026-11-09 · **Status:** Binding product target, not a claim of acoustic quality achieved
**Owner scope:** ArSonMix native built-in Harmony on draft PR #487. Preserve the approved compact Harmony editor, same .smix schema, native PCM ownership, and auto-rebinding DSP/GUI.
**Related:** [Natural Ensemble architecture](ARSON_HARMONY_NATURAL_ENSEMBLE_ARCHITECTURE_20261109.md) · [Harmony engine audit](ARSON_HARMONY_ENGINE_AUDIT_20261108.md) · [Depth engine ARD](ARD_ARSON_HARMONY_DEPTH_ENGINE_20261109.md)

## User outcome / job to be done
One live singer should remain **clearly in front and unmistakably themselves**. Enabling 3 harmony backing singers should sound like **three people standing farther back**—mildly left, inner-center and mildly right in a coherent shared acoustic space. Harmony must not replace, cancel, phase-comb or automatically duck the original singer. When harmony is muted/off/at Wet 0, the same dry vocal must be preserved exactly except declared latency/PDC, explicitly selected pitch correction and global Gain.

Users currently report a robotic/noise-like stack with little natural back/front separation. This is a P0 sound-quality defect, not solved by more panning or simply adding vibrato or wet reverb. The original 2-grain pitch-shifter is **still unqualified on singer recordings**; adding depth is a separate measurable, staged milestone.

## Native one-knob user workflow
- The single existing **Space** knob remains the primary distance/width control; no Room Lab, advanced switches, extra dialog, permission gate or mandatory mixer routing. Wet controls backing strength; Color controls timbral brightness; Gain remains explicit output gain.
- At **Space 0**: compact, center-dominant dry backing (little/no backing reflection), lead still forward.
- At **Space 0.6 (factory default)**: audible but unobtrusive backing group a few steps behind, with left/near-center/right separation. Depth comes from lower backing direct level relative to reflected/diffuse sound, smooth upper timbre, modest onset offset and coherent short room, **not arbitrary meters or fake 3D localization**.
- At **Space 1**: noticeably deeper backing; no excessively long tails, discrete echo, metallic ring, mono disappearing vocals or extra lead level loss. Space automation must not sweep variable delay taps.
- **Wet 0 or rack bypass**: backing and its room must not leak into the monitoring output. Switching the rack on/off must follow native state, no GUI-only fake effect.
- **Music / Manual Key**: existing authoritative audio-owner ACK/readback, observed note and auto-rebind remain intact. Changing Space while holding a note must not produce zipper/click.

## Audio precedence and separation requirements
1. **Lead invariant (P0):** Without user-enabled Pitch Tune, the sample-aligned lead path is independent of Wet/Space and retains channel polarity. Only the explicitly selected output Gain can scale both. Do not reuse a wet/dry crossfade that subtracts lead energy.
2. **Backing-only depth (P1):** all direct attenuation, high-frequency shading, early reflections and late diffusion apply to backing voices only. No additional effect may enter the lead path inadvertently.
3. **Direct-to-reverberant ratio (DRR):** deeper Space decreases backing DRR relative to Space near zero. This is a measurable signal-energy relationship, NOT a fixed literal physical distance. More reflection alone is not a substitute for quieter direct backing; no exaggerated reverb masking lyrics.
4. **Realistic front/back:** differentiated backing timbre without muffling consonants into noise; controlled stereo spread, mild independent phrases and natural pitch resynthesis. Existing 5–17 ms singer onset offsets and 12–20 ms early reflections need additional diffuse room energy to avoid hearing a single metallic/slap echo.
5. **Same room:** three singers share one subtle stereo diffuse field (with per-voice source position), not three unrelated giant reverb boxes. Late field is correlated acoustically but should not collapse to identical phase L=−R.
6. **Mono/speaker sanity:** mono fold of all wet voices stays audible and finite, no polarity inversion of dry signal, no excessive comb-filtering from source duplication. Test direct monitor + host monitored route externally: two simultaneous lead paths can still cause comb filtering outside Harmony, which DSP alone cannot cure.
7. **RT budget / portable:** keep current single JUCE PCM callback, bounded fixed memory and deterministic per-sample work. No fft/heap/mutex/host calls in realtime, no forced new global audio device, no new plugin PDC unless graph PDC tests pass.

## Objective acceptance gates (all required)
| Gate | Test and evidence | Release requirement |
|---|---|---|
| Lead preservation | Wet=0/45/100, Space=0/60/100, no Pitch Tune, native PCM difference against dry-latency baseline | Sample-exact or <=1e-6 numerical lead-path delta after removing backing; no silent wet lead duck |
| Backstage vs foreground | Record natural male/female adult singing phrases; loudness-matched blind A/B between Space 0 / 0.6 / 1 | Listener reliably ranks backing behind lead and three voices separate; report results, not invented 3m coordinates |
| DRR / spectral balance | Synthetic impulse + vowel, separate backing-only STEM before final sum, calculate direct/reflected RMS in defined windows, compare bands | DRR decreases with Space; no extreme high-frequency loss (sibilant intelligibility); log curve |
| Room behavior | 32–192 kHz transient and silence; inspect 0.3–1.5 s tails and decay | No audible flutter echo, metallic ping, runaway feedback, NaN, noise-on-silence or tail on Wet 0 |
| Mono compatibility | L+R fold-down, L-R, in-phase and anti-phase stereo; 2/3/5 voices, 64/128/256 frames | Lead unchanged, backing energy remains audible with no complete cancellation |
| Realtime | Exact Windows MSVC/CTest plus CPU profile and real ASIO 48/96k × 64/128/256 frames with FX rack open/close and device graph restart | No extra graph latency, XRUNs or callback p95/p99 regressions; report hardware measurements |
| Natural vocal quality (separate P0) | Consented vowel/consonant, breathy, low-bass, female-soprano, slides and vibrato recordings | Formant and intelligibility evidence; cannot call the ensemble natural before this gate |

## Milestones / explicit nonclaims
**D0:** Update PRD/ARD, isolate and remove wet-dependent lead duck, add mono/dry assertions.
**D1:** Prepared-sample-rate backing spectral shading + source-specific early reflections; one small bounded *backing-only* stereo diffuse late field; Space maps direct and reflected energy, not just width.
**D2:** Calibrate room decay/DRR and blind-listening A/B; profile worst-case Windows ASIO and return the simplest model that passes.
**D3 (separate P0 quality):** formant-preserving voiced/unvoiced resynthesis and expressive musical voice leading. D1 must not be marketed as completion of D3.

**Release rule:** CI passing proves code correctness; perceived 3-person background distance and human timbre need real recordings/listener evidence. Keep PR Draft until justified.

## 2026-11-09 D0/D1 working candidate (pending CI and ear-test)

Updated `HarmonyDsp.h` removes the silent Wet-controlled lead duck, preserves the already selected Pitch Tune semantics and builds an independent backing-only distance chain. The existing Space control now adjusts direct-versus-reflected ratio, gentle high-frequency contour and one shared late stereo ambience field in addition to moderate pan. This addresses the *architecture* behind depth—not a verified acoustic outcome. D2 listener ranking/DRR level matching and D3 formant-aware synthesis remain open; preserve Draft status.
