# P100 — Lyric Front Pocket (isolated audition, not release)
Owner: comfortable alpha.31 preferred to V5-stage-front, but lyrics become buried
without post-EQ 3.24k +3.3dB shelf. Tag e3e5eed5 differs from main 527e0216 only
by release documentation. Do not import #858's unapproved direct/tonal changes.

Evidence: Bradley/Sato/Picard JASA 2003 (https://pubmed.ncbi.nlm.nih.gov/12822796/)
early reflections can improve speech intelligibility; Biberger/Ewert JASA 2019
(https://pubmed.ncbi.nlm.nih.gov/31671969/) reverberation degrades binaural release
from masking. 2025 sung consonant study (https://pubmed.ncbi.nlm.nih.gov/41114603/)
shows preceding vowel reverberation masks consonants. Real live concert sung-word
recognition depends on masker and SNR (https://pubmed.ncbi.nlm.nih.gov/26520319/).
These studies motivate a wet-field experiment, not specific optimal filter coefficients.

Audit: P99 already applies attention-scaled center-late relief, with maximum
.108*strength (.42/.34) before bandpass. Avoid duplicating it. Replace ONLY this
operation with stable equal subtraction from both late ears: delta = .205671765
* normalized 1800Hz Q=.65 bandpass((L+R)/2), -2dB at center, low/high asymptotes
unity. All direct/early/source/FDN feedback and tonal filters remain identical.
P95 makeup and linked limiter remain downstream and may respond to the changed
output. No new FFT, delay, stems, audio graph, GUI or state ABI, no RT allocation.
One biquad plus scene slew; late FIELD mid is not a vocal source. Six warm scenes
4/5 only; Velvet Cinema, native bypass/MIX0 and ROOM OFF unchanged at steady state.
Room off resets tiny state; active scene transitions slew 55ms. Compile flag OFF
default, explicitly per-instance, original public release source-lock unchanged.

Qualification: assert 44.1/48k transfer, differential SIDE, pre-makeup direct/early
equal energy, full PCM 7 scenes at 128/256 block, input hard-left/anti-phase/mono,
MIX0 ROOM OFF, finite/peak and C50-like full-output impulse metric (not certified
ISO room measurement or STI). Log measured changes rather than claim listener
improvement from an impulse. Windows JUCE same-core parity, ACK/state and package
exact SHA. A/B on same scene with post-EQ identical off first, match loudness
then compare lyric transcription, comfort, warm body and stage.
Do not merge or release before owner listening acceptance and formal gates.
