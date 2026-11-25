# Anti Noiz — Product Requirements v1
Status: development / not admitted to the operator picker. Product owner goal: deep quiet between spoken phrases, natural clear/airy/full-bodied voice, intact singing and stereo music, real-time native ArSonMix FX.

## User journeys and priority
- P0 Mic input: learn room/interface hiss from 2 seconds of actual noise; reduce noise during speech, gently expand pauses; retain unvoiced consonants, breaths, quiet speech, body, clarity and air.
- P0 Stereo music: preserve stereo image, transients, quiet intros/outros, pads, cymbals and tails. Music Protect must be transparent (latency-aligned unity).
- P0 Mixed/unknown: prefer preservation over removal. No aggressive expansion when music or uncertainty is present. Separate mic and backing channels should be processed before summing.
- P0 Singing: conservative attenuation, protect vibrato, harmonics, falsetto and decay. No speech-only waveform reconstruction.
- P0 UX: compact Clean, Silence, Music Protect, Learn/Freeze, source mode, noise delta and actual reduction readback; approved /preview/ editor owner, no alternate mixer UI.
- P1 automatic context identification (speech/singing/music/mixed/noise-only, nonexclusive confidence), guarded adaptive profile, perceptual masking and optional zero-buffer live filterbank. No claim until corpus validated.

## Non-goals
Source separation, removal of competing voices, arbitrary transient removal, de-reverb, tonal excitation to disguise denoising damage, cloud/GPU dependence. No claim of perfect isolation from a mixed signal.

## Safety and acceptance
At a matched noise reduction, blind level-matched comparisons with RX Voice De-noise Music, RX Spectral De-noise, Brusfri and a soft expander. Targets (not measured): 10–18 dB stationary-noise improvement on speech where feasible, 25–45 dB idle reduction, no clipped onsets or damaged tails; music mode delayed unity, linked L/R gain, no expander closure on acceptance music corpus. Test real preamp/AC/fan noise, whisper, fricatives, cymbal wash, mono and asymmetric stereo, clean audio and speech over music. Measure P99/max callback time, no callback allocations/locks, 44.1/48/88.2/96 kHz, arbitrary block partition, latency and 24h stress. No release before native lifecycle, Windows, real ASIO listening and protected merge evidence.
