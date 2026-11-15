# ImerSip P0 — stereo-first MUSIC STAGE + physically effective controls
2026-11-15. Research only. Source-of-truth user FAT: prior P8.7 and P8.9 screenshot combinations at ~7m room source, -19.4° KEMAR ear alignment, and 100% reflections/reverb still had poor sound externalization relative to dearVR; ImerSip previous "all knobs max" sounded near-mono and worse than original stereo. Never call that a passed listening experience.

## Correct engineering goal
Stereo musical fidelity FIRST, externalized direct speaker orientation and physically measured room distance SECOND. No false assertion that P8.7 is dearVR quality. Official Sennheiser dearVR PRO2 info explains independent source distance, early reflections, late room and adjustable CLARITY as separate functions (https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2). The comparator is *human listening at equal loudness*, not large PCM delta.

## Specific root defects repaired
- Former stage WIDTH default 95% = 97/100°, leaving just 3° to max. New 32–126° front arc, default ~96°, with marked knob/source auditory difference.
- Former fixed virtual SOURCE distance 1.6 m regardless of user "externalize" intent. New 1.6–7.4m control maps to SonKu sourceDistanceMeters and actual wall path-time/level in SAME P8.2 early/FDN core, default ~5.08m.
- Old P8.7 height correction could be achieved only by moving physical XYZ source down toward the floor. ImerSip-only true KEMAR 3D direct HRIR trim -30..+20° is independent of physical room XYZ; default 0°, no hidden override of user's anatomy. At -19.5° tests compare actual ear PCM, not GUI animation. Ordinary SonKu rendering is unchanged.
- EARLY SPACE can now be exactly zero while LATE TAIL independently remains available. No hidden 20% early baseline. Max gains enlarged with core and optional output protection.
- Stereo KEEP retains original M/S music separation; more HRTF SIDE transfer is optional, not forced on all stereo content. Contrast with previous near-mono FAT.
- Tonal knobs with deliberately useful end stops: strong but bounded coherent original LF and ~2dB HF DETAIL above 5.6kHz. No synthetic bass oscillator, second FDN, FIR, FFT, graph reroute or new native engine.
- The former GUI deferred slider changes until mouse-up. New JUCE sliders queue the most recent edit through SPSC at up to 25Hz, completion ACK remains native audio-applied. Two horizontal source controls added to compact stage view, alongside eight correctly labelled rotary controls, A/B original, music-default, stereo meter.
- IMRS v5 (52-byte) carries distance and ear align; v1-v4 accepted sessions restore with independently initialized safe defaults. No overwrite of saved .smix, no auto mute.
- Windows portable package metadata now reports the ACTUAL 52-byte schema, 32–126° angle, 1.6–7.4 m distance and -30..+20° HRTF trim. Earlier stale v2 provenance had incorrectly marked a newly built binary as old 175°/1.6m renderer.

## Required gates
Native PCM 44.1/48k true stereo hard-pan/anti-phase/source MID, real speaker depth and angle sensitivity, 3D KEMAR elevation transfer, six-wall room path/FDN tails, exact bypass, output peak guard, chunk parity, SonKu P8.2 normal-path unchanged. Native JUCE real FX Picker, on-control PCM edit/ACK/readback, v1-v5 state restore, true Windows PE & exact same-commit portable artifact mandatory BEFORE a user FAT ZIP is promoted.

## Listening acceptance, not an algorithmic claim
A/B the ORIGINAL at the SAME monitoring loudness. Start with ~75% IMMERSION, 90–100° stage, 4.5–5.5m depth, EAR ALIGN 0° (audition gradual -10, -15, -19.5° separately), STEREO KEEP 100% and EARLY~50–70, LATE~20–35, OUTPUT 0 dB. Compare mono vocal focus, preservation of independent musical left/right, all transients, kick, ease of positioning speaker outside head. Do not maximize OUTPUT along with reverberation or mistake limiting for externalization. If the listener still hears it above/inside the head, the candidate is not acceptable merely because its numerical DSP tests passed. Generic non-individual KEMAR cannot guarantee another listener's exact pinna/head response.

## Isolation
Keep PR #706 draft until user approves headphones. Do not merge SonKu P8.3 experimental ancestry or change ASIO/WASAPI routing, device-recovery work (#713), GUI styling on other branches, or public release. 
