# ImerSip: SonKu P8.7 musical transfer with stronger front speakers

15 Nov 2026, private headphone research. Owner rejected prior ImerSip acoustic externalization.
Source: SonKu P8.7 draft PR #712 and ImerSip draft PR #706; do not merge experimental ancestry.

## Transfer
- Source-exact SonKuP87MusicalAirBody.h provides shared two-ear 3.9kHz +1.9dB direct shelf and 42% of true stereo FDN lowpass 175Hz wet body.
- The existing ImerSip 4-pole direct HRTF-difference crossover is moved from 700 to 480Hz to increase 1-4kHz speaker transfer; strict original bass bounds are unchanged.
- P8.6 four horizontal reflection paths remain, floor 24%, ceiling 15%. Late diffuse high frequencies are not boosted.
- Deliberately DO NOT stack P8.7's +9% original music bass on existing ImerSip bassImpact boost. Source output must pass 40-220 Hz guard.
- No new .smix version, no second JUCE engine, callbacks, devices, threads, FFT, FIR, synthetic bass, or P8.3 ancestry.

## Acceptance
- 44.1/48k PCM real source test probes P8.7 air/body spectra, exact test-only source A/B and real rendered changes, bass/mono/SIDE/anti-phase/chunk parity, 0% dry and peak safety.
- Native JUCE audio applied ACK / save/restore, actual Windows x64 ZIP provenance from the same commit are required before user FAT.
- Neither an RMS delta nor CI green constitutes perceptual externalization proof; only level-matched same-headphone audition can confirm outside-head quality.
