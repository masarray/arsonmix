# ImerSip — Stereo Keep must not disable externalization (15 Nov 2026)

Owner explicitly reports P8.7 requires extreme WIDTH, SPATIAL MIX, ROOM and REVERB just to START sounding spatial, and rejects near-mono ImerSip render despite max knob settings. The two user-provided screenshots are *research extremes*, not quality presets.

## Root problem in current ImerSip v5
While fixing previous near-mono music regression, the direct HRIR SIDE contribution was reduced to 10% at `STEREO KEEP=100%`, effectively suppressing the binaural cue even when `IMMERSION=100%`. That is not a valid independence of fidelity and externalization.

## Independent stereo fidelity / true direction
Measured two-ear KEMAR direct response from the existing P8.2/8.4 core is retained. The original dry stereo SIDE is NEVER canceled under KEEP mode. Instead, a separate four-pole 480Hz high-passed measured stereo HRTF SIDE is added at a bounded gain of 45% * immersion. Setting KEEP to zero restores the former full direct-HRTF transfer candidate; intermediate settings crossfade two different physical strategies. Original MID stays separately HRIR externalized. Bounded early/late P8.6 room and P8.7 music air remain, no fake delay/extra convolution/reverb, no changes to SonKu ordinary renderer.

## Qualification
Real PCM 44.1/48k assertions must establish stereo integrity on independent, anti-phase and hard-pan fixture signals, 0 ROOM width sensitivity with KEEP 100%, mono front stability, 64/128 chunk parity, original P8.2 unaffected, bass, peak, dry bypass and true Windows JUCE IMRS v1-v5 ACK/restore. A real 100% control must not have the previous ~10% HRIR SIDE restriction. No claim of audible dearVR equivalence without same-headphone owner audition. No release, branch remains PR draft until full Windows and user listening.
