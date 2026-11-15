# ImerSip 180 degree full measured stereo / not near-mono — 2026-11-15

## Owner FAT rejection
User screenshot shows FRONT STAGE / 126° with all knobs at 100%, Speaker Distance 1.6m, Ear Align +8.5°, Stereo 1.33x. User perceives the music as nearly mono and says knob extremes are ineffective. Audio result is rejected regardless of the prior green PCM delta tests. Latest SonKu P8.7/P8.9 screenshots are extreme experiment references, not validated sound presets.

## Source-level root cause
ImerSip previously fused physical L/R kernels toward a centered KEMAR HRIR at only **30%** directional strength before exposing direct ears; later ImerSip retained just 45% of the measured-side component at STEREO KEEP 100%. Effective source directional contrast could therefore be ~13.5% of the physical measured HRIR difference despite a max 126° stage. This compounds music stereo masking. A single "STEREO 1.33x" RMS metric cannot prove interaural separation or realistic speaker position.

## Repair
- Stage Width maps user 0..100% to 30..180 degrees; full = KEMAR +/-90 degrees, a LATERAL placement that can maximize separation but is *not* proof of frontal externalization. The visual top view, numeric readout, and actual source geometry use the same angle. Default user stage = 130.5° (67%).
- ImerSip-only KEMAR target HRIR correction keeps FULL measured difference for independently different L/R stereo sources while centering only the *common* mono HRIR component. Original SonKu path remains unchanged byte-for-byte; old P8.4 filter study remains available for unit provenance but is **not** attenuating the current ImerSip differential anymore.
- Original source L/R SIDE is kept on the direct music bed, with the existing LF guard, bounded measured SIDE addition, early/late P8.6 room and P8.7 air/body; no stereo widening using artificial Haas, positive/negative phase copies or another audio graph.
- Dedicated tests at 44.1/48k verify actual full +/-90 measured source output vs 30°, mono-correlated front and stereo preservation, 64/128 chunk parity, each useful knob endpoint real PCM delta, bass 40..220Hz, peak, saved sessions v1..v5, native mixer FX picker.
- Do not promote as equivalent to dearVR or subjectively immersive before real same-headphone owner audition and exact commit Windows JUCE ZIP provenance. Keep PR draft, do not change main.

Reference PR: https://github.com/masarray/arsonmix/pull/706

## Measured 180° HRTF and control strength calibration
The exact MIT-KEMAR measured +/-90-degree SIDE impulse energy was only 1.1548x the +/-15-degree impulse energy at 44.1 kHz. This is a physical HRIR measurement, not permission to invent different acoustic filters or assert an impossible >1.25x raw energy. Direct source still reaches true 180° angular directions. ImerSip's measured direct-side admission therefore now uses a bounded width-linked .20..60 coefficient, NOT the former flat 45% at every angle, so the full Stage Width endpoint produces a more meaningful actual PCM effect. Existing >=86% original music SIDE integrity regression is UNCHANGED. No source SIDE M/S widener is added.
