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

## P0 owner correction — audible benefit after Learn (2026-11-25)
ASKP-VST is not an acceptance baseline. Anti Noiz must not be so conservative that learned noise remains clearly audible while the user speaks. Measure active noise/error reduction separately from pauses with Silence disabled. Preserve desired foreground, not the noise itself. Default mic workflow should select Voice policy for an explicitly known mic source (or ask for source mode where routing intent is unavailable); unknown/mixed Auto must not silently claim active speech cleanup. Current Auto is still unity on foreground, a known product blocker for intelligent Auto. Do not release it as a fully automatic denoiser until the active policy is validated on real speech/singing/music. Do not use a universal aggressive mask on master/backing music. Clean 0 + Silence 0 is delayed unity; increasing Clean must produce meaningful extra reduction on learned stationary hiss. Mandatory synthetic regression: mono and independent stereo, 48 kHz harmonic foreground + colored hiss, >=10 dB active clean-reference error improvement at Clean 14 dB and >=11 dB at Clean 24 dB, projection 0.97–1.03, no expander assistance. This is a minimum fixture gate, not a real-voice claim. Real mic target remains 10–18 dB where feasible, with listening and consonant/tail preservation; idle 25–45 dB target. Reject a suppression improvement that fails preservation or real-time regression.
