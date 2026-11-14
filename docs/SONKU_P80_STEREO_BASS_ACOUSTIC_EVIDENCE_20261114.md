# SonKu P8.0 — Acoustic objective evidence and stereo-bass field test

**Experimental only. NOT dearVR-equivalent or approved for public release.**
User headphone feedback: P7.4 severe stereo cancellation; P7.5 shrill/prominent mid, thin bass, flat room, weak natural externalization.

## Code changes and limits
- Only opt-in `ARMIX_SONKU_EXPERIMENTAL_P8_TRANSPARENT_BASS=ON` together with P7.5 and native XYZ research. The default LIGHT/public plugin is untouched.
- Preserve original channel-specific low-frequency pressure through a prepared 220 Hz complementary one-pole crossover, while spatializing its remaining upper band through the measured center KEMAR HRTF. This is **not** a guessed +dB bass boost, but an original-signal transparency path.
- Return the highpass-only SIDE in the P7.5 protection bed, never doubling low frequency after restoring original L/R. At WIDTH 60° and ROOM OFF, a fully inverted stereo program should pass through at exact user trim without cancellation.
- Use actual x0/x1 stereo source inputs for the six physical early reflection images and the eight-line late diffuse field (instead of two duplicated MID inputs). Their room size/delay and source angular spread now follow the two inputs; sound-quality realism and rearward localization still need independent headphone evaluation.
- Remove prior shelf on the new experimental path so ROOM ON/OFF cannot activate a spectral boost. Dry-only MIX=0 should be mathematically exact independent of GAIN.
- No FFT, allocations, locks, background worker, extra bridge graph, or extra per-insert data movement in the realtime callback. Original KEMAR 140-tap FIR and native JUCE/ACK persist.
- **Still not a fully spatialized stereo object renderer:** above-crossover stereo SIDE remains a direct width bed. This is a targeted bass/room diagnostic milestone, not completion of P8.1 or P8.2.

## Bench and acceptance
- `tests/sonku_p80_tonal_room_evidence_test.cpp` records per-signal RMS transfer vs 63/125/250/500/1k/2k/4k/8k at 44.1 and 48k; hard-left and dual-mono; room OFF and ROOM ON impulse energy. It enforces finite, nonclipped samples, dry bypass equivalence, anti-phase energy, width 60° exact anti-phase waveform, room late tail and chunk partition parity.
- Log the source SHA and compile flags; ensure CTest source fixture and native JUCE adapter share P8 flag. Output metrics from synthetic waves are diagnostic ONLY, not target equalization, no real-music spectral score, no headphone claim.
- For safe physical evaluation: use **new isolated Windows tester**, start with **headphone volume low**, one P8 SonKu insert, ROUTE MAIN confirmed, Master Mono OFF, ROOM OFF, ORBIT OFF, SPATIAL MIX 100%, SIDE WIDTH 60°, GAIN -6 dB. Compare true plugin bypass vs P8 ROOM OFF, then ROOM Square 7.2m with Reflections 55 and Ambience 30, and afterwards dearVR with Bass Boost OFF and Clarity reported. Same licensed local track, output level matched and identical headphones/ASIO.
- A/B on (1) bass-heavy stereo drums/bass, (2) vocals, (3) acoustic reverb tails, (4) hard-left signal, (5) anti-phase high frequency and (6) wide chorus; *record source/output PCM dry and wet* if still thin, nasal or flat. If bypass itself sounds wrong, inspect channel mode and WIRE/MAIN/ASIO mapping first.
- Do NOT publish a Windows release or merge the original draft P7.4/P7.5 simply because synthetic CI passes. Further P8.1 full stereo positioning and P8.2 reference-calibrated broadband room remain mandatory for any dearVR-comparable claim.
- MIT KEMAR attribution in `docs/THIRD_PARTY_8D_SONKU_HRTF.md` is required in physical artifacts.

Architecture gate: `docs/SONKU_P8_DEARVR_BENCHMARK_ACOUSTIC_ARCHITECTURE_20261114.md`. The goal is original, independent implementations benchmarked to dearVR, not copying proprietary IR/Clarity algorithms.
