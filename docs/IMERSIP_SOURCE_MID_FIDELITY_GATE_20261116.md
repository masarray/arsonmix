# ImerSip v5 — source-MID music-fidelity gate (2026-11-16)

## Evidence and scope

The owner rejected a muffled, mid-forward, weak-bass, near-mono headphone presentation. Spectral tests of the BASS and AIR knobs alone cannot detect coloring caused by the measured direct HRTF path. Under `STEREO KEEP=100%`, `ImerSipDsp::process` previously added the full high-passed `deltaMid` between measured KEMAR stereo direct and original dry headphones. Its four 480 Hz high-pass stages still admitted upper vocal MID energy.

## Bounded audio correction

ImerSip alone now applies an additional three-pole 3200 Hz complementary high-pass to only the measured direct **MID difference**. The existing original dry MID remains primary at 500-2600 Hz. Measured direct **SIDE** is not attenuated by this change; high-frequency pinna-direction cues, source L/R bass, existing actual six-wall early returns and single 3-band FDN remain present. No new graph, second reverb, delayed bass crossfeed, heap allocation, modelled 'subwoofer', or device callback was introduced. Both smoothing coefficients and filter states are prepared/reset at supported 44.1/48 kHz rates.

This is an engineering candidate, **not** a headphone-validated improvement. Naturalness, vocal clarity, physical bass and externalization need a loudness-matched owner A/B. Higher binaural delta does not establish quality.

## Objective gate

New `verifyDrySourceVocalMidFidelity` checks the ORIGINAL source MID level for mono fixtures at 500, 1100, 1900, and 2600 Hz, at narrow and maximum stereo width, room and tonal controls OFF, 44.1/48 kHz, both 128 and 64 frame chunks. Target 0.89–1.12 relative MID RMS (level, **not** a promise of spectral transparency). Existing real HRTF, full-width SIDE, mono/anti-phase, room wet-delta, bass, AIR, peak, chunk parity, native state ACK and Windows JUCE gates remain mandatory.

## Hold and comparison

Do not retarget/merge PR #706 to production `main` or mix P9.1/P9.2 experimental world-bass code until physical headphone listening and exact Windows binary verification. Use equal-playback-loudness comparison: bypass all other spatial FX, compare original music and user-preferred SonKu P8.4 at identical volume, then ImerSip. Inspect vocal 1–4 kHz, low-frequency impact, treble detail, front-stage separation, and room decay as separate dimensions. The default preset values and saved IMRS v1–v5 state are unchanged by this tonal transfer fix.
