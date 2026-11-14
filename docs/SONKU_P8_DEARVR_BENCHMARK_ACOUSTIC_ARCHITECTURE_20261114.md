# ArSonMix 8D-SonKu — P8 Acoustic Fidelity RFC and DearVR PRO 2 Benchmark

**Date:** 2026-11-14. **Status:** RESEARCH DESIGN / NO PRODUCTION DSP CHANGES.
**Owner physical listening verdict:** P7.4 FAILED (severe cancellation); P7.5 FAILED PERCEPTUAL TARGET (thin bass, nasal/prominent mid, unnatural stereo consolidation, weak cube/room depth). Green source tests or Windows build do NOT override physical listening. **No claim of dearVR equivalence.**

## 1. Reference evidence: what dearVR actually does

Public documentation, NOT reverse engineering:
1. Official dearVR PRO 2 manual (Jan 2024): https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf
   - Chapter 2: spatial location alone is insufficient; distance, motion, first reflections, late diffuse reverb are separate acoustic cues.
   - Chapter 3.2: Realtime Auralisation computes reflections relative to LEFT/RIGHT/FRONT/BACK/TOP/BOTTOM boundaries; wall distances influence arrival delay/strength; independent reflection gain, high-pass and low-pass.
   - Chapter 3.3: 46 *captured room-character* presets, independently adjustable reverb size, high-pass, low-pass, gain. We do NOT need 46 presets for a believable first release: better 1 reference-calibrated room than 46 arbitrary labels.
   - Chapter 4: CLARITY trades HRTF spectral coloration against externalized/localizable perception (0=maximum localization, 100=minimum tonal coloration); BASS BOOST separately enhances input bass. **Don't credit room alone for dearVR's bass until BASS BOOST control state is known.**
   - Mono input is explicitly separate from stereo width; a mono track has only one positioned virtual source, while stereo input can retain two source markers.
   - Bass Boost, Clarity and Performance Mode cannot be inferred from screenshots; A/B must log their actual states.
2. Sennheiser explanation (2025): https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2
   - HRTF always changes the spectrum; the optimal balance is CONTENT DEPENDENT. Do not copy patented AMBEO Clarity algorithm or weights.
3. Sennheiser PRO 2 release: https://newsroom.sennheiser.com/dear-reality-releases-dearvr-pro-2-spatializer-plugin-p7zg0c
   - Proper stereo input and stereo-width positioning, distinct ER/late high-/low-pass, believable distance.
4. DAGA 2025 binaural auralization research: https://pub.dega-akustik.de/DAS-DAGA_2025/files/upload/paper/397.pdf
   - Plausible BRIR-style rendering may combine a GOOD HRTF, simplified geometrically consistent early image sources and a COMMON late diffuse RIR/reverb; measured RIR/RT60 and interaural room statistics are meaningful anchors.
5. DAGA 2018 externalization and HRTF/headphone dependence: https://pub.dega-akustik.de/DAGA_2018/data/articles/000252.pdf

**Legal design constraint:** Do not copy dearVR binary, measured response, presets/IR files, patented Clarity, GUI or proprietary kernels; use original algorithms and datasets licensed explicitly for redistribution. Commercial reference is a black-box listening comparator, not a source-code dependency.

## 2. Confirmed source-level architectural problems (P7.5 at 089520f43c7e)

| Finding | Existing source | Engineering consequence |
|---|---|---|
| P7.5 forces a stereo track's MID into ONE KEMAR directional center FIR, adds unfiltered SIDE as equal-and-opposite ear bed. | `SonKuSpatialCore.h::process` `musicSafe`, `sideRescue` | Prevents select anti-phase nulls but puts SIDE IN THE HEAD; the stereo soundstage does not truly occupy separate L/R positions in space. Does not preserve natural sound by construction. |
| ROOM feeds two copies of MID to reflections at zero spread; late FDN also gets MID/MID. | `SonKuSpatialCore.h::room_.process`, `reverb_.process` | ER and tail lack the original SIDE scene information. Stereo width control is *not* independently spatialized left and right sources. |
| Spectral balance is not designed from full-band calibrated reference response. A ~180 Hz one-pole ear shelf is nonzero in scene mode; legacy/off mode bypasses that compensation. | `SonKuSpatialCore.h`: `earBass_`, `smoothedBassShelf_`, `nativeSpaceEverUsed_` | Low frequency can remain weak when ROOM OFF; scene switching changes tonal balance as well as room. Mid presence cannot be fixed by simply raising broadband gain. |
| Room uses geometric 4+2 image reflection paths with fixed scalar wall gains and one mild low-pass, not calibrated frequency-dependent boundary absorption. | `SonKuEarlyReflections.h`, six-surface research | Six-wall coordinates ARE implemented, but physical depth is not guaranteed: time, direction, strength and spectra must be consistent with target room and loudspeaker positions. |
| Late room is synthetic eight-delay-line FDN with small fixed wet scale; three presets based on RT60/damping, no measured per-room RIR or independently controlled late spectral decay. | `SonKuMusicalReverb.h`: `wetLeft*.23f*amount_`, `prepared_` | Flat or artificial room impression despite presence of reverb. |
| Existing P0–P7.5 tests mostly use impulses and simple tones + stereo energy minima, not captured music and direct/room-separated transfer plots. | `tests/sonku_p75_stereo_integrity_test.cpp`, CTest | A non-null output can still be hollow, nasal, phasey or lacking externalization. |

Source pointers are claims about **this specific branch**, not claims that the commercial engine uses the same filter topology.

## 3. Target architecture — source-faithful, room-aware binaural renderer

```text
Input L/R or true mono
  ├── source-layout adapter (MonoObject | StereoObject | StereoBed)
  ├── calibrated spectral/phase safety model (control-side coefficients)
  │     ├── center/coherence-safe musical content
  │     └── truly positioned stereo object information (never unfiltered headphone SIDE as the sole width)
  ├── object-based direct field: interpolated measured HRTF(s)
  │     └── separately derived common timbral equalization / constrained localization cues
  ├── six boundary image paths with real delay, direction, frequency-dependent absorption
  ├── calibrated late diffuse/BRIR-like field (RT60 by octave, predelay, decorrelation)
  └── headroom/level-calibrated summation, stereo linked peak protection
         → JUCE native single-authority PCM → existing FX rack/WIRE/MAIN
```

**Critical invariant:** An *unfiltered SIDE rescue* is NOT the shipping stereo renderer. Explore a stereo-source aware 2-input/2-ear transfer matrix whose coherence, frequency-band energy and independent source locations are analytically validated. A single centered HRIR can be a safe diagnostic mode for mono; it is not a spatial substitute for stereo scene source objects.

**Timbre path:**
- Audit KEMAR dataset normalization (free-/diffuse-field reference), ear magnitude and group delay in at least 1/3-octave bands for all real source directions; do NOT assume the baseline `~ -7 to -11 dB below 200 Hz` comment is a sufficient calibration.
- Proposed original **Timbre Preserve** mode: bounded common-channel, minimum-phase correction prepared OFF the callback; spectrally smooth gains that preserve interaural time/level differences and salient pinna notch cues. Expose a controllable coloration/localization tradeoff AFTER a measured improvement, not a copy of proprietary CLARITY.
- Evaluate lows independently: bass retention 40–250 Hz, mid 250–2500 Hz and treble 2.5–10 kHz relative to dry/source and matched dearVR. Add *optional* bass enhancement only after the signal has correct neutral base. Compare dearVR Bass Boost OFF/ON explicitly, never infer the room created all LF.
- Preserve energy across coherent mono, stereo correlated, inverse polarity, hard-left/right, narrow and diffuse sources. No unconditional mono fold for a stereo object.

**Room path:**
- First calibrate **one** credible reference room with measured or defensible RIR statistics, e.g. modest studio (not “cube” merely in GUI). Treat each surface as a frequency-dependent reflection transfer and track image-source arrival direction, distance, and delay. Independent low/high cut for ER.
- Preserve a coherent physically meaningful direct/ER ratio (DRR), decay time and interaural coherence; enable 6-wall ER AND late tail via separate controls. A big late-reverb multiplier alone does not create a cubic volume.
- Prefer a hybrid room model (physically positioned image-source early paths + FDN/partitioned-convolution diffuse late field) with prepared coefficients. No copied dearVR impulse responses. Calibrate RT60 by frequency band (at least low/mid/high) and reflection density, rather than only global 0.78/1.28/2.05 s.
- Distances must alter direct level, spectral air loss, DRR, early arrival and late diffuseness consistently, without arbitrary full-range dips.
- Keep SPSC atomic accepted settings, deviceEpoch/graphRevision ACK, on/off tail causal reset, bounded audio callback, exact source SHA artifact provenance, and compatibility with .smix existing schemas.

## 4. Experimental process — do NOT mix multiple corrective algorithms into one uncontrolled test

### P8.0 — measurement and hypothesis isolation (BEFORE redesign)
- Add a deterministic offline **source → FX → measured ear PCM** renderer for the same exact experimental `SonKuSpatialCore` source used by the full Windows app. Ensure bypass reference and switchable direct-only, ER-only, late-only, all-in combinations.
- Output per-case CSV/JSON and licensed WAV stems; report sample rate, block size, source file hash, preset, mode, direction, elevation, room geometry, verified applied state and source commit.
- Generate 2-channel logarithmic sweep / multitone transfer, transient and broadband noise at 44.1 and 48 kHz and examine 40–16k band ratios. Test calibrated dry reference and separately captured actual music: bass guitar/kick, full-range vocal, guitar, dense mixed stereo, hard-left stem, mono, decorrelated ambience, anti-phase and partial-coherence material.
- Measure: loudness matched within 0.2 LU/0.2 dB where feasible, 1/3-octave left/right/mid/side spectral deviation, per-band coherence, integrated short-time side loss, true peak, phase/ITD/ILD/position, short-term comb-notch depth, peak-to-RMS. For room impulse: ER arrivals/six directions, DRR, T20/T30/RT60 by band, interaural cross-correlation (IACC), energy build-up, tail density and echo density. Use a stable window and comparable direct-only signal.
- Baseline A/B four states: source bypass; P7.4 failed; P7.5 failed target; dearVR PRO 2, matched binaural output at the same source angle and distance. Capture reference exact control values (Clarity 0/50/100, Bass Boost OFF then ON, ER OFF/ON, Reverb OFF/ON, input Stereo Width, Output Binaural, master gain and OS headphone EQ disabled).
- **Gate:** no acoustic feature coding until failure origin isolated to direct HRTF coloration, stereo summation, or ER/reverb composition. Source tests must recognize *audibly flawed* P7.5 rather than pass solely because RMS > 0.

### P8.1 — music and tonal fidelity
- Implement independent mono vs stereo source semantics and validated coherence/energy-constrained binaural matrix. Keep a direct-only tonal A/B gate; reversible feature flag.
- Offline-prepared timbre correction, no naïve wide-band bass boost or unstable subtraction of differently delayed dry/HRIR.
- Acceptance: no material instrument disappearing; test bass and mid ratios against bypass and dearVR with fixed gain, no extra transient clipping, controlled L/R stereo placement, no head-locked SIDE-only artifact as principal stereo renderer. Targets require measurements + real listening, not arbitrary single RMS threshold.

### P8.2 — room realism
- Make separately isolated direct, first-arrival six-wall ER, diffuse late field and common output; calibrate one reference studio room for plausible DRR/decay and direction.
- Follow-up listening for front/back externalization, real room size, tail naturalness, timbral smoothness and distance ordering. Only then consider additional spaces.
- Reproducible A/B room-only and direct+room examples, measured impulse plots and bandwidth-limited controls; verify no "large room" labels unsupported by acoustics.

### P8.3 — CPU and product qualification
- Benchmark callback p95/p99 CPU and XRUN at 44.1/48k with 64/128/256 where hardware supports, 1/2/4 instances, room ON, direction movement, WIRE/VST3 and restart. Prefer one authoritative JUCE PCM graph; no WebAudio simulation or alternate audio engine.
- Source-derived, block-invariant DSP, no allocations/locks/work queues on callback, shared coefficient cache prepared control-side, optionally partitioned FIR/convolution for room assets when profiling justifies it.
- Test normal Save/Open `.smix`, v1/v2/v3 migrations, FX rack OFF/ON, per-channel main/bus routing, stereo output mapping, and mono switch.
- Two-stage release gate: automated numerical/CI AND actual owner headphone evaluation matched to dearVR; multiple independent listeners recommended before any "comparable to dearVR" claim.

## 5. Physical A/B matrix: record both SonKu and dearVR settings

| Scene | dearVR (log exact state) | SonKu measurement | Pass criterion |
| --- | --- | --- | --- |
| Dry direct | Reverb OFF, ER OFF, Bass Boost OFF, Clarity 0/50/100, output Binaural | bypass, direct-only, room OFF, spectral profiler | Mid and LF coloration explained and controlled; localization maintained |
| Stereo objects | matched stereo width and FRONT/R/L/REAR, static | independent L/R tracks and phase/coherence | No disappearing instruments, no unreasonably head-locked side |
| Six boundaries | Realtime Auralisation with actual wall distances, ER ON, Reverb OFF | 6-path timing/direction, band-filtered energy | Reflection direction and delay track room dimensions |
| Captured room | selected Studio Room/Small Studio, ER+Reverb, preset/size/filters written down | one calibrated target room; direct/ER/late diagnostic | Audible room size/depth, natural decay, consistent DRR |
| Bass comparison | Bas Boost OFF + repeat ON | neutral + optional explicit enhancement | Never confuse bass processing with spatial realism |
| Hardware | headphone EQ profile and exact ASIO mapping stated | same physical interface/cans, no OS spatial enhancements | Level matched, low XRUN, no user-critical dropout |

## 6. Implementation guardrails and decision

- **STOP iterating UI/assets, arbitrary wet multipliers or forced MID folding to solve this audio defect.**
- Current P7.5 [PR #689] must stay Draft/research-only, and P7.4 [PR #688] may not be presented as successfully audio-qualified. Current public default/release is unchanged.
- Do not copy or use Sennheiser proprietary source/code/IR data. Benchmark by functional/output observations and independently licensed original acoustic models.
- **Owner acceptance matters:** hearing reports of nasal mid, lost bass and lack of cube depth are fail conditions regardless of source CI. Measured improvements are necessary but not sufficient.
