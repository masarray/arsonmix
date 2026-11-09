# ARD — ArSon Harmony FX Backing-Only Spatial Depth Engine (v1.0)
**Date:** 2026-11-09 · **Status:** Proposed architecture with staged implementation checks
**Scope:** Native built-in Harmony on PR #487; companion [PRD](PRD_ARSON_HARMONY_NATURAL_DEPTH_20261109.md) and [Natural Ensemble](ARSON_HARMONY_NATURAL_ENSEMBLE_ARCHITECTURE_20261109.md).

## Evidence-led acoustic model
Human auditory distance relies on **direct/reverberant energy ratio**, overall source level, spectral balance and interaural cues. Increasing reverberant ratio commonly signals greater distance; absolute physical distance cannot be inferred from a simple Space knob. First-arriving direct sound determines much of localization (precedence); a small number of obvious repeats can produce slap/comb effects rather than a coherent room. More high-frequency absorption is a mixing cue, but **air absorption over just a few metres does not justify extreme LPF**. For a live singer, masking and consonant smearing are unacceptable.

- Bronkhorst & Houtgast, *Nature* 1999, distance versus DRR: https://www.nature.com/articles/17374
- Zahorik review, distance, intensity, DRR, spectral and interaural cues: https://pmc.ncbi.nlm.nih.gov/articles/PMC4744263/
- Litovsky et al. review of the precedence effect: https://pmc.ncbi.nlm.nih.gov/articles/PMC4310855/
- JASA 2025 frontal distance study (DRR or spectral centroid best single cue): https://pubmed.ncbi.nlm.nih.gov/40928551/
- Sound on Sound depth mixing (level/EQ/diffuse reverb versus clarity): https://www.soundonsound.com/techniques/creating-sense-depth-your-mix
- iZotope mix depth: https://www.izotope.com/community/blog/what-is-mix-depth-how-to-create-front-back-space
- FDN theory / realtime late room: https://artificial-audio.github.io/portfolio/fdn/

## Before: audited exact C++ issue
`engine/builtin_fx/HarmonyDsp.h::process` mixes lead with
`(1.f - wet_*0.65f*voicedGain_)` despite the lead being dry; at Wet=.45 / voicedGain=1 this is about -3 dB lead. This is **gain duck**, not proof of phase cancellation. Current `HarmonyEnsembleSpace.h` adds one early opposite-side tap/voice but has **no diffuse late field**. At most it generates scattered echoes and pan; it does not control DRR as an acoustic scene.

## One-source, one-room block graph
```text
Mono mic PCM + accepted ChannelId / native graph
    |
    +--> phase-safe analysis / voice targets --> pitch shifters (2-5)
    |          |
    |          +--> independent singer onset / timbre / pitch modulation
    |                  |
    |                  +--> short direct backing L/R (Space-dependent attenuation)
    |                  +--> per-singer fixed-tap filtered early reflections
    |                  +--> ONE shared small stereo diffuse late field [FDN]
    |                       (backing send ONLY; fixed delay, stable feedback)
    |
    +--> exact delayed native L/R lead --> optional explicitly enabled Pitch Tune
                |                                      |
                +-----------------------------> sum w backing L/R
                                             -> explicit Gain -> existing PCM bus
```
Only existing `Settings::space`, wet, color and voices. No new scene schema, plugin routing, hidden monitor step, GUI Lab, or second ownership. `Space=0` disables audible room, not the valid PCM path. Diffuse field is never fed with lead.

## Time/frequency strategy
- **Front:** lead unchanged by wet, stereo and sample latency untouched. Pitch Tune explicitly replaces/corrects when user enables it; never implied by Wet.
- **Direct backup:** keep existing individually phase-offset 5–17 ms arrival, deterministic cent modulation and prepared pan L/C/R. At increasing Space, reduce direct backing gain mildly (candidate 0dB to -4dB) so backing remains intelligible but moves behind.
- **Backstage spectral contour:** one sample-rate-normalized low-pass in each voice with a smooth dry/filtered crossfade, evaluated on backing only. Candidate corner around 3–6 kHz and partial dry retention; do not physically claim air propagation at 3 m.
- **Early field:** use existing per-voice 12–20 ms fixed taps, with mild high-frequency damping and side distribution; no modulation of the delay tap by Space (avoids Doppler/comb chirp).
- **Late field:** one shared bounded 4-branch stereo feedback delay network (FDN), fixed integer delay lines with different non-harmonic lengths and a normalized Hadamard feedback transform. Feedback damping and per-line gain computed in non-RT `prepare(rate)` to target about 0.45–0.7 s decay. Conservative energy normalization, filtered output, no runaway or metallic tonal build-up. First prototype is room **ambience**, not cathedral/hall reverb.
- **Control:** `space` interpolates smoothly across width, direct gain, backing brightness, early/late send and return. `wet` scales backing energy only; disabled or Wet=0 must not leak old tail. All coefficients finite and clamped. No audio-owner reallocation or blocking.

## Proposed bounded C++ data ownership
- `HarmonyEnsembleSpace.h`: per-voice onset, identity modulation and 4096-sample early reflection rings; optional per-voice damping state. Precompute sample-rate coefficients on CONTROL `prepare(rate)`; callback only adds/multiplies.
- `HarmonyDepthRoom.h`: 4 shared, 8192-sample float delay rings (128 KiB); 4 damping states; 4 feedback gains; 1 monotonic write index. Delays up to 41 ms at 192k remain in bounds. A 4x4 orthonormal Hadamard transformation (scale 0.5) ensures controlled feedback energy. No `new`, FFT, `pow`, `sin`, `log`, device calls or synchronization in `processSample`.
- `HarmonyDsp.h`: keep lead path independent of `wet`; accumulate backing direct and early/late sends by voice, shared stereo room returns only in wet branch. Use existing 5-ms control smoothing and fixed instance state.
- Single publisher and callback: preserve bridge ACK/readback/graph lease, `ChannelHarmonyProcessor`, SPSC packets and `latencySamples()`. Declared PDC remains 1536 samples @48k until evidence warrants an independent P0 latency redesign.

## Risks, failure behavior and test design
| Risk | Defensive behavior / evidence |
|---|---|
| Room self-oscillation / metallic flutter | Precomputed feedback magnitude <1 and damping, orthogonal matrix; impulse decay < target floor by ~2 seconds; 32/44.1/48/96/192k CTest and extended silence |
| Mono cancellation / comb | Positive-polarity forward dry, no artificial anti-phase width; L+R output energy with 2/3/5 voices >0 and lead parity; evaluate ER frequency response with real voice |
| Space automation chirping | Interpolate gains/filter coefficients, **not** delay lengths; test 0->1->0 at arbitrary 64-sample boundaries, output finite without transient spike |
| Lead duck | Unit check dry equals original delayed PCM at wet=0,45,100 and multiple Space; actual lead equation cannot reference Wet |
| Reverb masks lyrics | Limit room gain, high-pass/LP spectrally soften only wet room; compare consonant intelligibility, DRR as measured against current impl |
| Real singer still robot | Keep depth independent; this architecture doesn't solve formant/grain synthesis; P0 resynthesis follows separate proof |
| Device restart / session | `prepare(rate)` resets room/filters and restarts deterministic state, no stale room tail from previous device; maintain GUI native ACK identity |

## No shortcut quality declaration
Do not equate "FDN compiled" with “feels like three people metres away.” First require baseline/updated dry and backing stems rendered at level parity, mono/stereo blind A/B for perceived front-back ranking, listening through nearfields/headphones/mono laptop speaker, and actual ASIO 48/96 kHz 64/128/256 buffers with full mixer FX rack and CPU p95/p99/XRUN data. The desired response is **distant but distinct, not washed out**. Exact room dimensions and HRTF positioning are not implemented and must not be shown as facts in GUI.

## 2026-11-09 implementation checkpoint D0–D1 (unqualified acoustically)

D0 implemented: `HarmonyDsp::leadPath` no longer references Wet; Pitch Tune still explicitly blends corrected lead. Direct backing attenuation is separate from dry lead. D1 implemented: `EnsembleSpace::shadeBacking` adds a rate-correct gentle LP shading to **backing only**; `BackingDepthRoom` adds a 4-branch fixed-delay, precomputed 0.55 s nominal FDN with frequency damping and non-anti-phase stereo outputs. Space smooths original pan, backing attenuation, early-field gain, late send and return. No new controls/schema or device calls; no extra plugin PDC.

Added CTest gates for 32/44.1/48/96/192 kHz impulse/tail feedback stability, reset/silence/mono compatibility, backing HF-shading boundedness and explicit pitch-tune lead preservation. This is an acoustically *unqualified* room candidate until real listening A/B and physical ASIO load.
