# SonKu P8.7 — Music Air + Resonant Body, private headphone FAT (2026-11-15)

## What the owner heard

P8.6 horizontal focus stopped the overwhelming room-filling image but the music seemed phase-cancelled, high frequencies constrained, bass thin and insufficiently rounded/resonant. The owner's perceptual observation is not itself proof of a DSP phase-error: ear-dependent HRTFs, HRTF/headphone spectral mismatch, Mid/Side, comb-filtered reflection addition, limited treble, level differences and nonindividual KEMAR may cause such sensations.

**Protect P8.6** (mode 5) as new focused-stage baseline; P8.5 (mode 4) for world yaw, P8.4 (mode 3) musical fusion, P8.2 (mode 0) golden fallback. P8.3 physical modes 1/2 remain rejected. No silent scene edits or preset defaults.

## Audit of actual DSP root

- P8.6 reduces floor ER to 24% pressure, ceiling to 15%, ER overall gain to 76%, FDN tail to 38%. This can remove diffuse masking at the cost of room body and spectral liveliness. Same actual KEMAR direct FIR and music SIDE were preserved, therefore *those* are not newly phase inverted by P8.6.
- Existing P8.1 horizontal KEMAR equalizer uses a **capped -5.5 dB broad 2.2k band** cut to restrain a measured resonance, and a modest high shelf at 6.2k. P8.7 deliberately does NOT inverse-filter narrow pinna notches or remove the original tonal calibration; broad presence/air deserves conservative audition when room is dry.
- Existing P8.4 fused stereo direct HRTFs share a center anchor plus 12%-bounded highpassed speaker difference. Existing music SIDE is head-locked; neither P8.7 nor P8.6 solves this limitation. Phase cancellations may persist from that model; do not claim otherwise.
- Existing physical horizontal 4-wall early paths already have measured HRTF and frequency-dependent material absorption. Simply raising all ER or late HF will restore the bad P8.5 room-filling sound.

## Research basis

Sennheiser explains that nonindividual HRTF timbre coloration is a trade-off against externalization and that an optimum per instrument often differs. We do not implement or copy their patented CLARITY algorithm: https://help.sennheiser.com/hc/en-us/articles/27424692616594-How-to-produce-natural-immersive-music-with-the-CLARITY-modul-of-dearVR-PRO2

Optimization of virtual loudspeakers and early reflection directions matters perceptually, particularly for small-room stereo music: https://www.mdpi.com/2076-3417/7/12/1282

Pinna filtering in direct BRIR supports externalization; pinna filtering in late reflected BRIR can reduce it: https://doi.org/10.1016/j.apacoust.2020.107257

2026 frontal NI-BRIR study emphasizes selective early reflection level and direct/room ratio rather than maximizing global reverberation: https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/

## P8.7 engineering candidate — one native DSP, private only

P8.7 is **renderMode=6**, gated by `ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT=ON` and `SONKU_P87_MUSIC_BODY_RESEARCH=1`, defaults OFF in public builds. Native JUCE v8 uses the **same 59-byte** mode/session state layout, retains v1-v7, rejects rejected physical modes 1 and 2. Only capability-gated source readback shows P8.7 beside P8.6/P8.5/P8.4/P8.2. The approved GUI and mixer source outside SonKu are untouched.

ROOM OFF: output from mode 6 MUST be **bit identical** to P8.6 across yaw/azimuth, stereo correlation and audio chunking. Controls, 220Hz transparent bass, all per-ear measured HRIRs, 0° direct elevation, world/head transform and four horizontal/floor/ceiling image paths are unchanged.

ROOM ON (mode 6 only):

1. Add a modest **+1.9dB asymptotic 3.9kHz first-order minimum-phase air shelf** after the existing KEMAR measured direct EQ, identical coefficient for L/R. This is a *broad shelf*, not an aggressive inverted HRTF pinna-notch EQ, hard clipper or excitation distortion. Phase lag/leads are frequency-dependent but L/R share exactly the same filter topology, therefore no arbitrary interaural delay is inserted.
2. Preserve original *un-HRTF-filtered per-channel* music LF below the 220Hz crossover, with **+0.75dB (1.09 pressure)** when ROOM ON. Avoid introducing a second separate bass synth or side-signal doubling.
3. Add **42% of a first-order 175Hz lowpass** of the existing **eight-line stereo FDN wet tail** to itself. This is an additional natural, delayed low-frequency room component. The existing FDN decay/direction, frequency-band absorption, ER gain, six-wall geometry and off/room state remain as P8.6. Critically, the previous full-band diffuse HF tail does **not** get broadly amplified.

No heap, mutex, disk IO, trig, FFT, extra 140-tap HRTF or second PCM graph in the native callback. Gains and poles are bounded and prepared off audio. A mode or room transition clears only four scalar memories inside the existing 12ms audio-controlled mode fade.

This is a **candidate**, not a proven cure for phase cancellation or equivalent to dearVR. Exaggerated bass, spectral harshness or perceived inside-head image would fail owner headphone FAT.

## Qualification gates

- Source native: 44.1k/48k, ROOM OFF exactly equal P8.6 for az 0/90/180, yaw 0/60/180, correlated/antiphase, 64 and 128 frame parity. ROOM ON differs, remains finite/bounded both 1.6m studio and 6.1m LARGE. Separate actual processor transfer scans verify slight higher HF and more lower FDN energy while not boosting high FDN content.
- Windows: actual JUCE v8 audio-applied ACK; reject mode 1/2, old v7 P8.6 mode5 preserved, old v6 P8.5 mode4 and v5 P8.4 mode3 preserved; native device reprepare, stale-session acceptance guard, same .smix state, 16 focused real native CTests. Proper embedded GUI resource verification and source-specific provenance/ZIP.
- Listening FAT: level-match, same headphones, same music, no external EQ/crossfeed. First ROOM OFF P8.6=P8.7. Then ROOM ON, 1.6–3m and owner's LARGE/6.1m, reflections 100%, reverb47%. Rotate 0°, ±30°, ±60°, 90°, 180°. Check high-band detail, consonants, transient, acoustic separation, kick/bass weight, phase sensation, vocal center, horizontal speaker pinpoint, floor/ceiling sensation, fatigue. Owner chooses P8.6 if newer version loses clarity, focus or musicality.

No public release, no production merge and no claim of subjective completion before owner listens. Because the branch stacks on P8.6, later approved integration MUST forward-port clean changes onto current main without bringing dormant P8.3 modes into production.
