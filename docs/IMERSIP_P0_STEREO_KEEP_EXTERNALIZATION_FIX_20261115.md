# ImerSip — Stereo Keep must not disable externalization (15 Nov 2026)

Owner explicitly reports P8.7 requires extreme WIDTH, SPATIAL MIX, ROOM and REVERB just to START sounding spatial, and rejects near-mono ImerSip render despite max knob settings. The two user-provided screenshots are *research extremes*, not quality presets.

## Root problem in current ImerSip v5
While fixing previous near-mono music regression, the direct HRIR SIDE contribution was reduced to 10% at `STEREO KEEP=100%`, effectively suppressing the binaural cue even when `IMMERSION=100%`. That is not a valid independence of fidelity and externalization.

## Independent stereo fidelity / true direction
Measured two-ear KEMAR direct response from the existing P8.2/8.4 core is retained. The original dry stereo SIDE is NEVER canceled under KEEP mode. Instead, a separate four-pole 480Hz high-passed measured stereo HRTF SIDE is added at a bounded gain of 45% * immersion. Setting KEEP to zero restores the former full direct-HRTF transfer candidate; intermediate settings crossfade two different physical strategies. Original MID stays separately HRIR externalized. Bounded early/late P8.6 room and P8.7 music air remain, no fake delay/extra convolution/reverb, no changes to SonKu ordinary renderer.

## Qualification
Real PCM 44.1/48k assertions must establish stereo integrity on independent, anti-phase and hard-pan fixture signals, 0 ROOM width sensitivity with KEEP 100%, mono front stability, 64/128 chunk parity, original P8.2 unaffected, bass, peak, dry bypass and true Windows JUCE IMRS v1-v5 ACK/restore. A real 100% control must not have the previous ~10% HRIR SIDE restriction. No claim of audible dearVR equivalence without same-headphone owner audition. No release, branch remains PR draft until full Windows and user listening.

## Default tuning correction
Prior v5 used 76% immersion, 68% width, 70% early, 34% late and 60% depth. It left default musical scene too restrained relative to the user's P8.7 reference. New opt-in default (not a hidden migration of saved sessions): 88% immersion, 82% width (=109.1° frontal angle), 78% early, 25% late, 78% depth (=6.12m), 100% stereo keep, bass 50%, air 40%, 0dB trim, 0° KEMAR ear correction. Old IMRS records remain restored exactly; UI double-click default matches C++ initializer. No blanket 100% reverb and no hidden output boost.
