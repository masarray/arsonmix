# SonKu P8.6 — Horizontal Speaker Focus (private FAT candidate, NOT released)

## Owner headphone FAT → measurable hypothesis

P8.5 Windows portable (sha `121fcc514c660327e29054c32266d2ed5ba54600`, Actions `43659462670`) creates **slight genuine perceived externalization**. Compared with dearVR PRO2, however, music feels too large and diffuse, as if speakers fill floor and ceiling instead of being identifiable ear-height virtual loudspeakers that can rotate around the listener. Preserve P8.5 as the newest physically preferred baseline, P8.4 as musical reference, P8.2 as fallback. **P8.3 remains rejected**.

The source-backed difference is not that the HRIR direct source has an unexpected positive elevation (owner tested 0°). Rather, when ROOM is enabled, P8.5 renders six material-coloured measured reflection paths, including ceiling and floor, plus a diffuse eight-line FDN. The scene applies separate gain laws `1.45+2*reflection` and `2.2+4*ambience` to these fields. At LARGE / reflections 100% / reverb 47%, such a large diffuse room can mask the directional direct image. This is an acoustic **hypothesis consistent with the report**, not a proof of perceived causality. The HEAD frame yaw mismatch was already fixed in P8.5. Stereo SIDE still remains partially head-locked and generic KEMAR/headphone compensation can create unrelated localization errors.

Sennheiser says dearVR's CLARITY tunes the spatialization–timbre trade-off for different sound material; its proprietary method is not copied: https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2. Relevant review of directional, reverberant and head-motion cues: https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2024.1408073/full and https://www.frontiersin.org/journals/neuroscience/articles/10.3389/fnins.2023.1027827/full .

## P8.6 algorithm and isolation

Use new research-only mode `renderMode=5` with `ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT=ON` and `SONKU_P86_HORIZONTAL_FOCUS_RESEARCH=1`. Production default OFF; no change to old modes 0/3/4 or user's scene/preset. In mode 5 only:

- Keep **exact P8.5 direct two fused HRIRs**, head yaw world→head, music SIDE, preserved original bass/crossover, tonality, output gain, limiter, PCM engine and convolution. Do **not** increase speaker contrast or invent crossfeed/phase shifts.
- The native six-wall room still processes all six paths and uses their original geometry, delay, HRTF coefficients, RT60 material and IACC cues. Four horizontal walls remain **100%** of their existing path response. Floor path is **24%**, ceiling path **15%** of P8.5 reflection pressure; neither is hard-muted. This specifically reduces *up/down* phantom energy without setting direct elevation to an artificial value.
- Independently lower the P8.5 **ER presence coefficient to 76%** and diffuse **late presence to 38%**. Keep both wet components separately audible, retaining room identity and distance. No EQ, limiter makeup, automatic loudness shift, extra FIR, additional audio callback, heap, locks, or new codec.
- Native v7 retains identical 59-byte state structure. Prior v1-v6 states load with old mode 0/3/4 intact. Reject P8.3 modes (1/2) in bridge and v7 state. GUI editor exposes P8.2 / P8.4 / P8.5 / P8.6 only after identity-matched native v7 capability/readback. No preset reset in A/B.

Quantitative coefficients are **research starting points**, not optimized by ear or claimed equivalent to dearVR. Do not ship a GUI preset with forced reflections 100%, or silently rewrite any existing user state.

## Required gates

1. Source-native C++: P8.6 ROOM OFF must be bit-exact P8.5 at 44.1/48k, yaw 0/60/180, azimuth 0/90/180, correlated and anti-phase material, and 64/128 sample blocks.
2. Source-native isolated per-wall PCM impulse test: four horizontal walls P8.6/P8.5 energy = 1.000 each; floor = 0.24² = 0.0576; ceiling = 0.15² = 0.0225, and both nonzero. ROOM ON measured PCM difference and preserved finite output.
3. JUCE v7: queued audio-side mode change, accurate applied ACK, state save/restore, old v6 P8.5 recovery, disallow 1/2, device prepare, FX bypass, and native host unchanged.
4. Windows real executable, approved embedded GUI resources, source SHA and signed PE architecture, portable ZIP, CTests and exact run artifact; **not** approved on Linux only.
5. Perceptual FAT cannot be replaced by PCM directionality. On the same headphone/song and matched measured level, compare P8.5 vs P8.6 at yaw 0, ±30/60/90/180 with STATIC, 60° spread, 1.6–3m ROOM OFF/ON first, then owner LARGE/100%/47%, including intentional FRONT/LEFT/REAR/RIGHT. Rate ear-height source pinpoint accuracy, separation, music body, bass, vocal, fatigue and width. If P8.6 is too dry/internal or weak, P8.5 wins. Retain immediate A/B rollback.

## Known limitations / non-goals

No real head tracking from manual yaw. No personalized HRIR/headphone correction or measured BRIR. High music SIDE remains partly headphone-head-locked. Room reductions may improve directional prominence yet reduce natural externalization; this is why source-only CI does not establish success. No merge to main, no public release and no claim of dearVR parity before FAT. The branch is a quarantined stack on P8.5 research; after approved FAT, extract a clean production-safe patch without rejected P8.3 code ancestry.
