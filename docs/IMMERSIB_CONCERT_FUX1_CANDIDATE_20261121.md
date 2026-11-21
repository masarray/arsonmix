# ImmerSIB FUX-1 concert stereo + vocal clarity candidate

**Upstream baseline:** FUX-1 PR #853 at `1b724a4b19105486fa42ef9ab4b541fcb7bc49fb`; coordinates issue #854 and delivery #837. This is a **candidate, not an accepted or public audio revision**.

## Intent — scope boundary
The operator has confirmed that ImmerSIB's concert ambience, large venue and deep bass **already sound good**. The remaining problems are L/R-panned instruments moving toward the center and vocal direct body/presence/intelligibility at 200–300Hz, ~1kHz and ~5kHz. Input is a finished stereo master, NOT stems. Preserve all seven factory scene parameters (stage >100°), existing P91/P92 world bass, all existing KEMAR matrices, original room/early renderer, IMRS/.smix wire identity, and all FUX-1 built-in mixer editor viewport interaction.

## Implemented experimental DSP (flagged off by default)
Build flag: `-DARMIX_IMMERSIB_RELEASE_CANDIDATE=ON -DARMIX_IMMERSIB_CONCERT_CANDIDATE=ON`. Explicitly rejected without real ImmerSIB product. Per instance opt-in from `ImerSipP92Processor`; regular SonKu and the ordinary public ImmerSIB build use the original PCM arithmetic.

1. **P90:** the existing coherence-driven middle-HRTF projection must also pass a difference-to-total energy purity test. This prevents strongly correlated panned sources (L=x,R=.25x) from being mapped to the same centered HRIR as true L=R mono. The default P90 behavior is unchanged; first non-silent evidence primes the candidate detector without a slow startup pan shift. There is still one 2x2 measured HRTF/direct path and no extra dry SIDE.
2. **P98:** the existing scene4/scene5 2.05/3.45kHz SIDE masking attenuation is omitted only for the explicitly opted-in ImmerSIB candidate, while its existing ~285Hz vocal body/center-source shaping remains. L/R stage width remains whatever the saved/factory scene specifies. A SIDEBAND cut intended for vocal masking is NOT a vocal source separator in a stereo master.
3. **P99 ImmerSIB adapter:** apply small, fixed-state IIR bandpass relief to only the already-rendered **LATE MID** at ~1050Hz and ~4900Hz. Preserve vocal DIRECT body, late-room SIDE, directional EARLY field and all LF reinforcement. This is relative spectral unmasking, not boosting output or adding a vocal stem. No new delay, FFT, lookahead, second render graph or allocation in the audio callback.
4. **Native C++ test:** pure C++ 44.1/48k coherence/panning vs mono, source SIDE spectral energy, late vocal 1k/5k unmasking and relative LF integrity. Real full-program headphone listening remains mandatory and is NOT simulated by this test.

## Qualification/provenance
- `config/immersib_approved_pcm_sources.json` and `scripts/verify_immersib_approved_pcm_sources.py` are **intentionally NOT modified or relaxed**. Because acoustic source files changed, the public **alpha.26 approved source gate will correctly reject this candidate**. Do not bypass it, rewrite old SHA, or merge as a public release until an acoustic revision is accepted and a NEW provenance baseline is created.
- Before shipping: actual exact-head Linux/Windows C++ and native P9 CTests; run 7 scenes with dense full stereo music, true center + highly panned accompaniment simultaneously; compare matched-loudness A/B against alpha.26 and current FUX-1 pre-candidate (headphone); collect ear ILD/ITD, phase/comb, correlation and mono/anti-phase, DRR/early/late, peak limiter and gain trace; hardware latency/ASIO FAT is separate.
- Explicit release status: **NO installation artifact / no public PCM promotion / no hardware-qualified sonic claim** from these commits.

This branch is a research-ready product integration candidate, not a claim that musical quality has already been confirmed.