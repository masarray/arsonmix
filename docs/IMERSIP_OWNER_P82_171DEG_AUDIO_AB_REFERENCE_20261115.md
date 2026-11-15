# ImerSip subjective A/B reference — owner screenshot 15 Nov 2026

This document locks the **actual reported owner-preferred sound**, not a synthetic guessed benchmark.
Screenshot: ArSonMix `v0.5.0-dev+g29ac1dde`, 8D-SonKu editor on CH 2. No PCM/audio WAV is embedded in the screenshot and we cannot objectively claim the screenshot proves phase, frequency response, loudness or speaker externalization.

## User's starting-point reference: SonKu P8.2
- Existing FX rack: `dearVR PRO 2`, `ImerSip`, `8D-SonKu` on CH 2. Verify bypass state of other spatial processors when doing controlled A/B. The screenshot is **not proof** all three are simultaneously audio-active.
- 8D-SonKu editor: STATIC, clockwise label displayed but motion OFF; SIDE WIDTH **171°**; SPATIAL MIX **100%**; OUTPUT **+2.5 dB**.
- ROOM preset **SQUARE**, dimensions **7.2 × 7.2 m**, distance **1.6 m**, elevation **0°**; REFLECTIONS **100%**; REVERB **47%**.
- Owner says this is reasonably good musical starting point, but needs still greater outside-the-head speaker externalization and clearer detail; audible phase cancellation remains problematic. No subjective acceptance for first ImerSip version, which was hollow/mid-muffled/near-mono.

## Actual upgrade strategy, not visual equivalence
1. KEEP original stereo music/direct center vocals as authoritative, not reconstitute them from averaged two displaced binaural HRIRs. The first ImerSip released in research was overly wet/centered at 60° and failed owner's music listening. In current code: `original MID=(L+R)/2` has no KEMAR center-HRTF or wet reflections (except bounded optional DETAIL at user demand); raw `SIDE=(L-R)/2` always survives.
2. For optional external space, ImerSip may compute a genuine **pair** of P8.2 measured KEMAR filters at ±(80°..180°)/2. IMPORTANT: enabling this per-ImerSip research mode must not change the normal 8D-SonKu build/sound. Only the binaural SIDE differential is mixed into ImerSip at low level. User's width 171° corresponds to ImerSip WIDTH=91%. The current 95% default =175°—a deliberately close stage starting point.
3. LOW BASS: preserve source channel-coherent 25–220 Hz (no Haas or extra room-modulated low bass), and provide a bounded positive LF shelf through BASS IMPACT. Never claim perceptual unchanged bass without A/B.
4. MID/VOCAL: preserve exact source MID at DETAIL=0, BASS=0 and unity output, with spatial width and room at maximum. All virtual ambience must stay as differential SIDE; reject large new mono nulls.
5. DETAIL: optional controlled high shelf (above ~5.6 kHz), not default midrange exciter. Subjective brightness preference is not assumed.
6. Use JUCE built-in native editor, no fake JS DSP or competing WebAudio callback. Control gesture → SPSC → audio applied ACK; Save/Open supports v1→v2 migration.

## A/B field protocol on user's real Windows headphones
- First bypass dearVR PRO2 and ImerSip; listen to **only 8D-SonKu P8.2** with EXACT numbers above. Fix device, rate (44.1 or 48k), buffer, gain, channel and songs.
- Then bypass 8D-SonKu and any other spatial processor, enable **only ImerSip** on the same channel/route. Start Concert, immersion 58%, width 91–95%, space 45%, bass 75%, detail 0%, output matched by ear or a loudness analyzer. Avoid stacked processors; do not A/B a +2.5 dB track with a unity source and mistake louder for better.
- Alternate processing blind where possible on the same music fragments: (A) centered vocals/piano/snare, (B) anti-phase/independent stereo instrumentation, (C) bass kick 25–220Hz, (D) front lateral/back perceived localization, (E) sibilance/upper HF fatigue, (F) real sampled room-tail reverb and transients.
- Acceptance: stage outside head convincingly, MID vocal uncolored; no mono-like collapse or hollow 1–4k presence; bass full; original left/right percussion localization intact; no audible pitch/combing, overbright harshness or clipped output; CPU stable at 44.1/48k.
- Tests and acceptance criteria differ: native PCM mathematical invariants/CTests can detect certain errors; cannot certify physical headset HRTF transfer or externalization for the listener. Real perfectly anti-phase source is mathematically silent upon mono sum regardless of a transparent processor.

**Do not merge into public main or replace P8.2 reference** until the Windows JUCE full candidate passes and owner physically accepts the listening comparison.
