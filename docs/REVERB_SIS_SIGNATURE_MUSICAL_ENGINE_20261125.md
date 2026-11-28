# Reverb-Sis Signature Musical Engine — 2026-11-25

Base 72f51779081d5a01f76bd316b5eb56c58f874246 (verified Windows FAT 45478630808). Scoped follow-on to #886; reconcile #892 shared Main.cpp hunk before consolidated release.

## Design and evidence gates
Owner requested fuller low, subtle air, no Album Ready toggle. Same single FDN, wet-only 170 Hz +1.5 dB bell Q 1.05, retain 55% of extra historical Album LF absorption, +12% existing bounded air-shelf lift (not +12% total HF amplitude). No new feedback gain, dry EQ, FFT, lookahead, callback, allocations or delay line. Preserve HP/LP, mid cuts and dynamic de-ess. Dark settings remain authoritative. Profile transition 45 ms; stable profile bypasses morph update, hoist per-frame feedback loss and reuse duck fraction. CPU improvement is a hypothesis until same-runner A/B.

New and all 16 factory presets use profile 2, retaining preset IDs/numeric settings for controlled A/B. Legacy .smix AVRB v1/v2 and .armixfx missing/0/1 retain original/Album sound; v3 and voicing 2 identify Signature. No hidden automatic migration; selecting factory preset explicitly adopts Signature; user preset recall preserves its own profile. Unknown values rejected. No Album Ready button. Original C ABI/default core paths stay compatible.

Research: https://www.fabfilter.com/help/pro-r/using/decayrateEQ ; https://valhalladsp.com/2011/05/18/valhallaroom-the-late-controls/ ; https://www.izotope.com/products/aurora ; https://caml.music.mcgill.ca/~gary/618/week3/node11.html . Independent design, no proprietary code copied.

Tests: legacy and historical Album cross-build PCM fingerprints, Signature filter contour, profile automation/no heap stress, 6-band RBJ analysis of actual late-only wet IR at 44.1/48/96k across 4 algorithms, energy deltas and Schroeder T20 extrapolation (not ISO room RT60), native state/GUI/preset schema checks, same-runner 32-case timing vs historical Album. Keep all existing tests. Full Windows and physical ASIO/listening still required. Do not claim sonic superiority, actual CPU improvement, or artifact until evidenced. Tune preset-specific values only if audition/measurement identifies a concrete problem. No main merge or release from isolated candidate.
