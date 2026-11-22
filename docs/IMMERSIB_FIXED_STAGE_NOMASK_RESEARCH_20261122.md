# ImmerSIB Fixed Concert PA — rejection of v5/v6/v7 spectral tuning (22 Nov 2026)

**OWNER A/B STATUS: unapproved**. Do not release or merge on synthetic DSP tests.
Public alpha.26 on `main` remains the last acceptable rollback reference.
Rejected draft #857 must not be merged, automatically rebased or shipped.
This document is a code/experiment record, not a claim of musical success.

## Observed listening failure and traceable source risks

- Owner heard greater mid ringing and a frequency-shifting masking impression
  in v7 than the released version. Sinus response tables and filter-unit PASS
  did not test this perceptual failure, nor headphone transfer.
- Main P90's broadband 80ms correlation detector retargets *DIRECT FIR*
  every 16 samples. Correlation depends on the arrangement/music; therefore
  its center transfer varies with programme content. The candidate suppresses
  this modulation rather than inventing a vocal-object detector.
- Main P98 scene 4/5 P98 source `maskedSide` subtracts the S signal in
  narrowish 2050/3450 Hz bands. It also adds a 2050 Hz center boost. As the
  L/R relationship changes, different instruments can be relatively
  attenuated. The new candidate does not touch S or add center 2k energy.
- Main ImmerSIB P99 vocal room adapter uses level/side-dependent attention
  to reshape source, warmth feed and late 1.35–3.8k room content. Candidate
  does NOT opt this adapter in. Plain SonKu P99 remains untouched.
- Stage P97 also adds 1.9k center source/direct presence. Candidate removes
  only that extra mid component; LF punch, stage room air, early images and
  world bass remain. P95 auto makeup is disabled only for candidate to
  prevent changing gain with programme level; user output trim, original
  clipping safety limiter, and ASIO remain.

## Candidate audio path

Finished stereo L/R master → unchanged stereo positions of existing **TWO
virtual stage PA loudspeakers** (current scene spread: 104°–134° total,
world front) → prepared measured MIT KEMAR HRIR pair per virtual speaker →
existing directional six-wall first images + late concert FDN + P92 LF →
original linked peak protection. 7 scenes, .smix identity and /preview/
are unchanged. There is no fake stem separation, SIDE masking, frequency
following detector, time-varying HRTF center kernel, extra path, chorus,
Haas delay or manual chain of EQ notches.

P98 285Hz Q=.72 **single existing bandpass** adds a modest static source
body to original L and R independently (~0.16/.19 times its band output).
An uncorrelated hard-left source remains hard-left *at the input of the
virtual speakers*. No equal MID signal is injected into the right channel.
These are signal-domain linear operations only after the scene transition
slew settles. Source-dependent limiter clamping can still happen at peaks.

P80 direct HRTF uses only its original preparation-time calibrated 2.2k
correction, extended by a **maximum extra 1.2dB in the single existing
filter** for the candidate; this is not a headphone-response measurement.
No new 3k/5k filter. The correct band/weight requires actual listening
and additional ear-domain measurements; treat this as a low-risk ablation
hypothesis, not proven optimization.

## Qualification gates

1. `ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH=OFF` by default; requires explicit
   `ARMIX_IMMERSIB_RELEASE_CANDIDATE=ON`. No release provenance/source-lock
   waiver; no changes to the approved 8D-SonKu product or stable catalog ID.
2. Standalone source tests check static P90 HRTFs with changing correlation;
   hard-L, antiphase, matched warm response at 285/2050/3450/5500, and
   unmodified 0–3 voicings at 44.1k/48k.
3. Full-PCM source test checks all seven ImmerSIB scenes x four stereo
   sources x two sample rates, limiter finite bounds, 128/256 frame parity
   and programme-equivalent published-core reference (including old P99).
4. Windows must compile real JUCE native processor/editor, CTest ACK/state,
   actual mixer EXE, current /preview/ and legal/icon gates, SHA/flag-verified
   separate portable artifact. A successful test is NOT a listening PASS.
5. AB at equal perceived loudness, low listening level: check central vocal
   body and intelligibility, side guitars, sibilance, bass impact, concert
   envelopment and long-term fatigue on several real music masters.
   Repeat after a long A/B passage to expose programme-dependent spectral
   or level artifacts. Headphone-specific responses differ.

## Scientific grounding, boundaries

- Non-individualized HRTFs introduce audible spectral coloration and
  localization ambiguity: https://pmc.ncbi.nlm.nih.gov/articles/PMC7509635/
- MIT KEMAR generic HRIRs are not headphone-EQ measurements; individualized
  HpTF/ear-canal response influences outcome:
  https://www.mdpi.com/2076-3417/10/14/5014
- Virtual room perceptual dimensions include reverberation and interaural
  coherence, not simply width or global treble:
  https://pmc.ncbi.nlm.nih.gov/articles/PMC2730711/

**Rollback:** published alpha.26 main or ImmerSIB bypass; back up .smix.
Never replace the installed live mixer with this standalone candidate.
INT-1 #837 and primary ASIO are not blocked by this research lane.
