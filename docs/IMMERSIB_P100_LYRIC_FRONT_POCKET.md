# P100 Lyric Front Pocket — isolated acoustic candidate
Owner reports alpha.31 pleasant but vocal diction buried in room; V5 detail is reference only.
Baseline main 527e0216 has no DSP changes in comparison with v0.5.0-alpha.31.
Do not combine with unapproved #858. Existing P99 remains on.

## Evidence and design
- https://pmc.ncbi.nlm.nih.gov/articles/PMC11384524/ — early reflections can support intelligibility, late reverberation can reduce it, room cues contribute to distance.
- https://pmc.ncbi.nlm.nih.gov/articles/PMC6593929/ — binaural/temporal integration depends on reflection timing and direction.
- https://pmc.ncbi.nlm.nih.gov/articles/PMC6375510/ — C50 early/late energy ratio; not a lyric-intelligibility guarantee.
- https://aes.org/publications/elibrary/elibrary-page/?id=18001 — human music vocal clarity listening study.

One stationary low-Q constant-peak bandpass-derived cut on the existing FDN wet return MID after baseline P99. 1.8 kHz Q=.60, -2 dB at center, shared subtraction on both ears so wet SIDE difference is invariant. No source detector, new HRIR, direct EQ, new delay or audio graph. Existing P95 adaptive gain and limiter remain; final output may differ indirectly. FDN onset is not identical to a strict 50ms split; therefore do not label this as measured C50 improvement without an impulse test. No stem extraction.
Enable only branded candidate CMake flag, OFF default. No new state schema, GUI, or published-source-lock hash. Reset filter on inactive room/mix and core resets.
## Gates
Response measurement and wet side invariance; actual core all seven scenes at 44.1/48 kHz, block parity, ROOM OFF/MIX0 equivalence, early impulse parity, bounded total delta and no nonfinite/clipped samples. Windows JUCE exact matching oracle and portable provenance. Existing source lock expected to reject unapproved candidate files. No public release or merge until owner listening passes.
Human A/B matched perceived loudness, same song/scene/PEQ, compare intelligibility by lyric transcription and long-session comfort. A low-Q wet-only cut is an experiment, not proof of sonic improvement or individualized HRTF correction.
