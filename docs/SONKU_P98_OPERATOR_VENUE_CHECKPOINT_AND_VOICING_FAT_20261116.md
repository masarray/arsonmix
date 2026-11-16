# SonKu P9.8 — operator-approved Venue lock & Signature/Studio/Cinema voicing

**Operator audible acceptance (16 November 2026):** VENUE · MEGA SPACE produces goosebumps. Preserve **exact** P9.7 master scene on branch `checkpoint/sonku-p97-venue-mega-space-approved-20261116` at `fda0eef669e0a59575c489e149662c1bf878c998`. Do not edit that branch, its preset (10m, 106° spread, Large 28.8m, 100% room, 100% ambience, gain 1.8) or P9.7 engine. No release/main merge before user listening.

## Specific requested retune

1. **Cinema · Grand Stage** (7.5m, room 100%, late 84%): more credible low-frequency pressure/impact without adding exaggerated broad midrange reverb.
2. **Large Studio · Live** (4.8m): only a little extra punch/warmth.
3. **Signature · Immersive** (3.1m): flagship musical clarity, upfront center-panned vocal/instrument detail, punch/body, coherent first-reflection air; should compete with or perceptually outperform Venue *for music*, not simply louder output.
4. **Venue**: sacred approved P9.7 baseline, no additional DSP and no changed preset numbers. **Live Orbit** also neutral unless explicitly requested.
5. "Vocal/instrument separation" is a gentle center-panned tonal emphasis only. There is NO stem demixing or true vocal isolation.

## Architecture

A named, typed native **`sceneVoicing`** integer 0..3 removes all fragile guesswork based on floating-point distance/reverb. 0=approved Venue/Custom neutral, 1=Signature, 2=Live Studio, 3=Cinema. Browser only picks complete scenes and sends them via existing native FX transaction; **native readback/ACK** must validate type. Existing custom .smix, P9.7 presets, and older state must not be rewritten behind the operator's back. A native P9.8 candidate **v12 (67 bytes)** *append-only* stores the explicit voicing; existing v1..v11 load as neutral 0. Older P9.7 binaries still use v11 and refuse unsupported properties; UI removes sceneVoicing when capability absent. P9.8 is OFF by default and requires P9.7, P9.6, P9.5 stack.

The same authoritative measured two-world-speaker KEMAR PCM engine processes:
- **Center clarity:** modest doubly bandpassed 2.05kHz center sum injected into the two virtual speaker source feeds pre-HRTF. Never dry to headphone MID. Enhances *all* centered elements (vocal, kick, lead), not a voice stem.
- **Low-frequency pressure:** low-Q 86Hz punch and smooth 130Hz low body *after* the current P9.5 dynamic makeup, and *before* existing sample-peak guard; no subharmonic oscillator, no modified room RT60/feedback or headlocked LFE.
- **Directional external air:** modest highpass 5.2kHz enhancement on actual two-ear early reflection outputs, and gentle coherent per-ear sparkle in the final world binaural field; no synthetic Haas crossfeed.
- **Scene-specific gains, at full articulation:** Signature: center .22, bass body .15, kick .17, voice .15, ER air .16, end air .07; Studio .035/.045/.055/.045/.04/.02; Cinema .075/.15/.15/.06/.065/.035; Venue ALL ZERO. These constants are *audition hypotheses*, not perceptual ratings.
- All gains interpolate over 55ms, coefficients calculated only during prepare, fixed memory, no extra JUCE processor, FFT or delay.

## Gates

- Output sample peak <=0.9805 at 44.1/48k, front/back, real **wet** Signature/Studio/Cinema/Venue and ROOM OFF/0% MIX.
- Full integrated P9.7-vs-P9.8 per-sample **PCM hash exact** at approved Venue 10m with 100% room/ambience and existing 106° spread, plus ROOM OFF and 0% MIX.
- Cinema LF component >P9.7, Studio modest LF enhancement, Signature LF and vocal presence >P9.7 with no severe loss of treble. Compare at input matched and *also* match final loudness in owner headphone FAT.
- Native Windows JUCE schema v12 ACK/round-trip and v11 restore default0; invalid enum/rejected JSON, device restart and corrupt 67-byte snapshot; Chromium editor native mode/ACK; exact-head portable artifact.
- Operator FAT must verify actual perceived live stage, singer intelligibility, bass tightness/transient decay and no presence harshness or limiter pumping. Do not claim Signature is better to everyone's ears, or that a pre-defined room is a measured cinema/hall BRIR.
