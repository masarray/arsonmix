# SonKu P8.2 — six-wall multiband acoustic research / physical FAT

2026-11-14. **Research only, not a public release.**

The owner reports that P8.1 audio quality is much better than P7.4/P7.5. The P8.1 tonal and LF preservation code is an immutable A/B checkpoint; the separate P8.2 build must not replace it without physical listening approval.

## Implementation

- Same full stereo L/R and measured KEMAR direct HRTF, P8.1 original bass retention, and bank-derived presence compensation. ROOM OFF must remain unchanged.
- Six physically delayed directional image paths: left, right, front, back, floor and ceiling. Each uses a separate three-band intensity absorption material. Pressure reflection = sqrt(1-alpha), with original two-ear HRTF interaural cues intact. The band crossovers are 250 Hz and 4.8 kHz, after each path's binaural HRTF.
- One shared original material model drives a cheap 8-line stereo FDN with prepared, per-delay LF/MID/HF feedback gain and the Eyring room-decay formula. All sqrt, log, exp and coefficient design occurs at prepare time, not inside the PCM callback.
- Mode is isolated under ARMIX_SONKU_EXPERIMENTAL_P82_ROOM=ON and requires the three previous XYZ, P7.5 and P8.1 research switches. All switches default OFF. No second PCM, worker, heap, mutex, GUI DSP authority, FX state schema change or session migration.
- The parameter tables are **illustrative**, not measured RIR or copies of dearVR. Estimated LF / MID / HF RT60 in seconds: Studio 0.717 / 0.392 / 0.231; Chamber 0.933 / 0.802 / 0.483; Hall 1.716 / 1.219 / 0.650. These are model predictions only, not verified measured T20/T30.

## Automated gates

1. Source P8.2 test checks material absorption and pressure identity, geometry, theoretical RT60 ordering, time-domain six-wall image reflections, early/late energy, LF/HF decay separation, and native dynamic Room + motion chunk parity at 44.1/48 kHz.
2. Existing P8.1 frequency/anti-phase/bypass evidence must PASS again under the P8.2 flag, plus previously passed XYZ/native ACK and v1/v2/v3 state CTests.
3. Full Windows JUCE executable, approved embedded GUI resource hashes, exact source commit, PE provenance, MIT KEMAR attribution notice and FAT guide checksum must match the uploaded portable artifact.
4. **Not yet passed:** real room impulse response EDC, measured DRR, IACC, T20/T30, blind loudness matched dearVR A/B, physical ASIO p95/p99/XRUN and real music listening. Synthetic tests alone cannot certify equivalent sound.

## Listening A/B (separate folders)

Keep the P8.1 artifact (Actions artifact 14117224898) in its own writable extracted folder; unpack P8.2 separately with portable_mode.txt. Low headphone volume, ASIO 48k/256, real stereo input, WIRE to MAIN, Master Mono OFF, one FX insert, ORBIT OFF, SIDE WIDTH 60 degrees, GAIN -6 dB. Compare direct and bass with ROOM OFF first: P8.1 and P8.2 must sound the same.

Then enable Square 7.2, Reflections 55%, Ambience 30%, source 1.6m; audition solo vocal, bass/drums, acoustic percussion and natural recorded room tails. Change room to 14.4 and 28.8 and move XYZ near each surface. ROOM ON should increase credible spaciousness, direction and decay without making center vocal hollow or bass thin.

For dearVR PRO 2 comparison, record actual Stereo Width, source XYZ, room reflection and late reverb settings, Clarity and Bass Boost OFF and ON separately. Match loudness and headphone/ASIO chain. If P8.2 sounds worse than P8.1, stop and reject this experimental build; do not increase blanket wet gain.

No release, merge, or 'dearVR-equivalent' claim until owner approval and objective/perceptual room tests. Architecture: docs/SONKU_P8_DEARVR_BENCHMARK_ACOUSTIC_ARCHITECTURE_20261114.md.
