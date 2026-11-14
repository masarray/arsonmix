# SonKu P8.3 — Real Speaker Externalization (analysis, architecture, qualification)
2026-11-14 | DRAFT research gate, no shipping DSP modification
Depends on owner-approved music tonality P8.1 and P8.2 six-surface room research.
Reference implementation branch: feat/sonku-p82-sixsurface-multiband-room-20261114 (751ff03fa27fbf1cf623eda002af50978c7888e5). Preserve its exact Windows artifact as A/B anchor: 14118867259.

## Owner-reported physical observation / acceptance pivot
P8.2 music sounds good, BUT sounds remain inside/on top of the head rather than like physically external speakers at FRONT/RIGHT/REAR/LEFT. This is an *externalization / localization* failure, not evidence of insufficient global reverb, bass or output loudness. P8.1/P8.2 tonal work MUST NOT be rolled back; new room gain multipliers, brute bass boosts and moving the source elevation arbitrarily are NOT admissible fixes.

## Grounded research — sources and their actual implications
1. S. Best et al., Sound Externalization: A Review of Recent Research, Trends in Hearing (2020): https://pmc.ncbi.nlm.nih.gov/articles/PMC7488874/. Nonindividualized HRTF quality, pinna spectra, head movement, distance/reverberation, headphone EQ and listener differences all change externalization. No single low-level EQ solves every listener.
2. Brimijoin et al., The Contribution of Head Movement to the Externalization and Internalization of Sounds, PLOS ONE (2013): https://pmc.ncbi.nlm.nih.gov/articles/PMC3846779/. Stable WORLD-fixed scene under actual head rotation matters; head-locked sound tends to internalize. Head tracking is valuable, but it must not be confused with a source-orbit knob.
3. Auditory spatial assessment in real and virtual environments, scoping review (2026): https://link.springer.com/article/10.1186/s43163-026-01045-z. Elevation and front/back confusions are worse with generic HRTFs; individual BRIR/HRTF and head motion improve some comparisons. This is NOT a guarantee for any given user.
4. Enhancing Externalization of Frontal Virtual Sound Sources through Level and Reflection Adjustment, (2026): https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/. Controlled binaural levels and early reflection relationships improved 0°-30° externalization in the tested nonindividual BRIR setup. Treat as an A/B hypothesis, NOT a universal gain recipe.
5. dearVR PRO 2 official Clarity explanation (2025): https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2. A tonality/externalization tradeoff exists; optimize with music, not only white noise. Never copy the proprietary Sennheiser AMBEO patented CLARITY weights or engine.
6. Valve Steam Audio open docs: https://valvesoftware.github.io/steam-audio/doc/capi/guide.html and https://partner.steamgames.com/doc/features/steam_audio. Demonstrates distinct per-source binaural rendering, directional early reflections and optional custom HRTFs through SOFA. Third-party IR/code license review is mandatory before any distribution.
7. Waves Nx product description: https://www.waves.com/plugins/nx. Demonstrates a separate speaker-room-monitoring use case, real head tracking and optional headphone calibration. Reference its *design goals*, never its proprietary DSP.

## Source-confirmed root cause candidates in SonKu P8.2
A. **Strong architectural defect for SPEAKER simulation (confirmed):** `SonKuSpatialCore.h::process` with MUSIC_SAFE/P8.1 uses a single centered measured HRTF on `xL+xR`, plus an unfiltered/in-head highband SIDE term `sideRescue = (xL-xR)/2 * width`. This protects musical stereo energy but it is NOT the ear transfer matrix of two real loudspeakers in a room. Even perfect ROOM does not place the SIDE component in a stable external source. Keep this P8.2 behavior as proven musical fallback, not an external speaker mode.
B. **HEAD-LOCKED scene (confirmed):** `degrees_` is source azimuth in listener coordinates, updated only by source motion, not `az_world - headYaw` or full listener orientation. No head-motion sensor/native quaternion/OSC tracking pipeline exists in this SonKu code. When the user rotates their head, a real speaker should remain world fixed; current SonKu audio rotates WITH the head. Both static binaural mode and future dynamic mode require separate qualifications.
C. **Ear elevation vs perceived overhead (plausible, not root-caused):** direct elevation defaults to 0° and horizontal MIT KEMAR is used at 0°; `SonKuEarlyReflections` creates correctly *elevated* floor/ceiling HRIR paths. Their loudness, early arrival, head-related pinna notches and generic-ear mismatch are currently NOT shown to be perceptually weighted against the direct source. Controlled CEILING ONLY / FLOOR ONLY / NO VERTICAL ER A/B is required to determine if overhead perception is actually driven by these paths. Do NOT disable them globally based only on a hunch.
D. **Generic KEMAR limits frontal/rear perception (well supported, listener specific):** one dummy-head HRTF is used for every listener. Frontal 0° and rear 180° both yield symmetric interaural energy, so ITD/ILD alone cannot distinguish them; listener-specific pinna/elevation spectral cues and dynamic scene motion matter. In the 48k source table, at azimuth +90° the measured RIGHT ear RMS coefficient energy (~4642) exceeds LEFT (~951), so the *horizontal left/right ear sign is not obviously reversed*. Do not "fix" by swapping stereo channels.
E. **Direct and ROOM do not share the same stereo source model (confirmed):** direct is one MID HRTF + headlocked SIDE, while six-wall reflections run two sources at ±SPREAD. Direct and reflections can therefore describe *different physical emitters* for the same input music. ROOM gains/RT60 are plausible but not enough for a real stereo loudspeaker image.
F. **P8.1 common-ear EQ was tuned for tonal integrity, not externalization (known limitation):** measured 2.2k cut reduces harshness but might soften some localization cues in individual listeners. Do NOT disable this approved music fix casually; compare corrected/uncorrected DIRECT at level matched with controlled HRTF pinna-band and head tracking variables.
G. **Mapping/driver check required:** ASIO output L/R correct, Master Mono OFF, no Windows spatial enhancement, no extra dry/bypass FX route in parallel, no duplicated WIRE/MAIN or monitoring mix. Routing errors can create phasing unrelated to HRTF, and must be ruled out physically before attributing everything to spatial DSP.

## P8.3 architecture: two explicit, non-conflicting render meanings

### Speaker Monitor (NEW experimental output mode, targeted use case)
- Input L and input R are audio played by TWO physically fixed virtual loudspeaker objects, e.g. symmetrical ±30° in front at 1.6-2.0 m, nominal ear-height elevation 0°, with adjustable geometry. Each speaker contributes to BOTH ears through a measured, delay-consistent 2×2 binaural transfer (BRIR or HRTF + matching image-source room).
- Output ears: `out_e = H_(e,Lspeaker) * input_L + H_(e,Rspeaker) * input_R + physical ER/late`. Preserve inter-speaker coherence, lowband stereo/bass integrity, sensible trim and headroom; naive summing two coherent delayed FIRs can cause combing and old P7.4 failure. Evaluate proposed 2×2 transfer against deterministic mono, correlated stereo and stereo anti-phase with *per-frequency complex response*, not an RMS-only "non-null".
- Each actual source's direct, first arrivals and diffuse energy MUST refer to the same spatial object + geometry. Only BRIR-derived or time/level-justified all-pass/crossfeed may alter stereo coherence, and only if dry music passes tests. Keep P8.2 fixed control-side coefficient preparation and six-wall material, never add GUI WebAudio output authority.
- Mono input must have explicit semantics: one MONO_OBJECT at single XYZ, or duplicated stereo music presented through L/R SPEAKERS (requested monitoring), not silently swapped.
- This mode requires real FX format selection and versioned native settings/ACK migration, defaulting legacy session to original mode. Do not infer modes from L/R sample correlation in realtime.

### Music Bed / Music Integrity (P8.2 already approved for music)
- Keep original centered MID + headphone SIDE and transparent bass fallback as a separate verified listening mode, not falsely labeled external speaker.
- Compare against Speaker Monitor at equal LUFS/true peak and keep easy rollback; never destroy this behavior in the first trial.

## Head orientation / world-fixed pipeline
1. Separate `sourceAzimuthWorld`, speaker objects, listener `HeadPose{yaw,pitch,roll,time,valid}` and source-orbit state. Listener rotation is NOT source movement.
2. Geometry from native control path uses `directionListener = inverse(headRotation) * directionWorld` for BOTH direct and directional first reflections; old no-tracking pose is identity. Implement yaw-only proof first then pitch/roll, avoiding sign mistakes at ±180° and ±90°.
3. Optional native OSC head tracker (or a separately qualified webcam app) produces *bounded, time-stamped, immutable pose snapshots* via the existing command/SPSC mechanism, not ad hoc socket reads in the audio callback. Never silently treat stale trackers as valid; freeze last valid orientation briefly, then fade to static head-frame fallback, with UI indicating tracking LOST.
4. No tracker? Keep manual yaw A/B virtual head-rotation control for deterministic evidence only, not a pretense of real world-locked speakers. Ship tracking OFF by default without specific owner hardware.
5. For head tracking, test input-to-aural pose latency, continuity, angular velocity, jitter, no cursor/UI timing causing audio XRUN, and correct static room geometry under head turns.
6. HRTF profiles (multiple *license-cleared* SOFA datasets) are a future opt-in; listener A/B select the best one. No blanket head-size or ear-size guessed EQ and no unlicensed measurements packaged. Headphone EQ similarly opt-in and after verified native output chain.

## Diagnostic gates before changing DSP
P8.3A / dry native evidence:
- Log exact P8.2 source commit/settings; 48k and 44.1k direct-only mono impulses at [0,+30,+90,+150,180,-150,-90,-30], elevation 0, plus +/-20 elevation; log left/right impulse peak sample, normalized 1/3-octave magnitude, ILD by 250-2k/2-8k band, ITD time-lag/cross-correlation, and front/rear spectral contrast. No unsupported claim that ear energy alone proves front/rear position.
- Separately capture (a) direct, (b) horizontal ER ONLY, (c) floor ONLY, (d) ceiling ONLY, (e) diffuse late ONLY, (f) combined with EXACT same input. Numerical per-path energy and arrival time, late DRR/IACC/EDC. Restore the exact P8.1 room-off tonal transfer, and prevent the difference test from simply measuring arbitrary reverb boost.
- Tests run the real SonKu source, never a separately coded JS spatializer. A local offline measurement harness must expose **read-only stem taps** or a deterministic compile-flag diagnostic render, not duplicate audio authority in production.

P8.3B / first real stereo speakers:
- Mono speaker FRONT and REAR and ±90; stereo speaker ±30 with 0° ear-height elevation. L-only, R-only, duplicate mono, anticorrelated, narrow stereo, wide music and mixed bass+side, verify preserving instruments, proper interaural direction sign, bounded comb notches and true peak.
- Subjective randomized headphone position test with FRONT/RIGHT/REAR/LEFT + 4 diagonal positions, recorded as actual external vs in-head or over-head, perceived elevation and source distance. Collect baseline P8.2, Speaker Monitor, and dearVR PRO 2 under matched Clarity, Bass Boost and room settings. All 3 fail if unresolved head/above-head localization persists.

P8.3C / world lock:
- Rotate simulated listener yaw +15/-15/+90: a world-fixed FRONT source must appear RELATIVE -15/+15/-90; verify geometry under wrap and full XYZ. Repeat with live sensor and owner turning head (if available).
- Far/near consistency of DRR, first image travel time, attenuation and physical room; room can never rotate with the headphone frame.
- CPU p95/p99, XRUN at 44.1/48k and 64/128/256 frames for 1/2/4 inserts, plus WIRE/MAIN, device restart, .smix Open/Save/migrate, real JUCE APP and approved UI; process() cannot allocate/lock/load SOFA/call OSC or create a second PCM.

### Proposed acceptance (to calibrate with owner, not a current PASS)
- ROOM OFF sound at least as musically full and natural as current P8.2 benchmark; center bass, kick, vocals survive across mono/stereo polarity.
- 4/4 cardinal directions correctly distinguished in a blinded set, with 8-direction confusion matrix and frontal/rear and elevation failures reported rather than averaged away. Aim >=80% cardinal accuracy across several trial repetitions; a fixed generic HRTF may not meet this for all listeners.
- A clear subjective preference for source OUTSIDE the head, at plausible ear-height ±10° for nominal horizontal speaker positions, compared with P8.2; no persistent "speaker on top of head" report.
- If a world-fixed/head-tracked mode is provided, perceptual position must remain stable when the head turns and auto-fallback must be audible-click-free. Audio vs video sync must not be assumed.
- A/B dearVR on same audio, actual settings, same headphones and matched loudness, one MonoObject source first, then Stereo Speaker mode. Do NOT claim parity until repeated human listening supports it.

## Release and task sequencing
P8.3A = **measurement and hypothesis isolation only** (quick Linux native, not full Windows installer).
P8.3B = **new 2×2 virtual speaker binaural renderer** behind feature flag, preserving music-integrity fallback and front/rear direction; full Windows CI only after offline audible evidence.
P8.3C = **optional world-fixed head tracker and HRTF selection** after fixed speaker simulation proves direction; no silently-required tracker or purchased hardware.
P8.3D = **physical qualification** and dearVR comparative test with owner.

**Hard veto:** Never merge P8.3 into main, overwrite P8.2 physical candidate, or say "as good as dearVR" because CTests pass. No arbitrary reverb, bass, loudness or source elevation hack. No copied proprietary product DSP, IR, presets or patented CLARITY processing.
