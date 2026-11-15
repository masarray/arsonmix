# ImerSip P8.6 FRONT-SPEAKER DIRECT TRANSFER — research candidate (2026-11-15)

## Owner FAT regression and root cause

Actual owner screenshot at 100% IMMERSION, 180-degree STAGE, 100% VENUE SPACE, 100% BASS/DETAIL: still virtually no spatial externalization. Previous ImerSip rendered a measured P8.4-fused direct HRTF in SonKuSpatialCore but explicitly THREW IT AWAY on output. Then reinserted full original headphone stereo and merely added SIDE widening and six-wall room. Simply reducing floor/ceiling energy with P8.6 does not externalize a headphone-dry source.

## Isolated corrective candidate (never affects ordinary SonKu, no extra DSP graph)

- Same P8.2 single SonKuSpatialCore and real 140-tap measured MIT KEMAR pair. The opt-in ImerSip RoomReturns now includes **the actual DIRECT EAR pair** before generic scene attenuation, alongside existing six-wall early and 3-band late returns. Only the ImerSip caller requests these; normal SonKu output and public session contract are unchanged.
- Full immersion **replaces** the dry upper-frequency headphone feed with the binaural front loudspeaker direct signal. Complementary 260 Hz one-pole split retains coherent original bass below crossover, without a parallel full-band dry/HRTF comb filter. 0% is exactly unmodified original; mid positions are smoothly slewed. This is a physical headphone audition candidate, not an equal-power calibrated BRIR.
- ImerSip-only P8.4 high-pass directional fusion uses its supported upper bound 30% (formerly 12%) to produce substantial interaural transfer. Ordinary SonKu/P8.4 experiment is unchanged.
- P8.6 horizontal six-wall focus (four walls 100%, floor 24%, ceiling 15%) stays active. Early directional room is emphasized and late diffuse FDN restrained to avoid 'sound from ceiling/floor/room everywhere'.
- Actual front speaker geometry maps WIDTH to 40–100 degrees total (+/-20..50), **not** unrealistic +/-90-degree side-of-head loudspeakers. The JUCE stage diagram and numeric label now show the same geometry as the DSP.
- The ImerSip-only final linked peak guard protects the actual binaural direct output at high mastered levels, instead of clamping the DIRECT substitution back to dry; it is a sample-peak limiter, not a true-peak/oversampled limiter. NO allocation/FFT/mutex/thread on the PCM callback.

## Verification and field acceptance

- Native 44.1/48 kHz two-ear direct-signal test with ROOM OFF, exact original SonKu OFF parity, mono voice, anti-phase/stereo, bass/no null, dry bypass, chunk parity, venue/wall energy, and actual non-negligible direct-vs-dry PCM transfer.
- Full real Windows JUCE CTtest + native executable ZIP must pass **on the same commit** before user download is promoted.
- These tests prove DSP routing and bounds, NOT psychoacoustic quality. Headphone localization depends on listener-specific HRTF/headphone response, and static scene without head-tracking is inherently limited. Run loudness-matched A/B against preferred SonKu P8.4 and P8.6; user physical listening is the release gate.

References: SonKu P8.6 PR #711; ImerSip PR #706. Public research: https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/ and https://journals.sagepub.com/doi/10.1177/2331216520948390.
