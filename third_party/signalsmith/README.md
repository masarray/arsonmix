# Signalsmith libraries vendored for ArSon Player M2

This directory contains **verbatim**, version-pinned upstream C++ files for
worker-thread pitch/time stretching. Both projects are MIT licensed; their full
licenses are included next to these headers. No runtime download, Python package,
additional audio callback, or network operation is used by ArSonMix.

| Component | Exact upstream commit | Included files |
| --- | --- | --- |
| Signalsmith Stretch (version 1.3.2) | `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` | `signalsmith-stretch.h`, `LICENSE-STRETCH.txt` |
| Signalsmith Linear | `7bb1c8f36794d6980080ce817c91f8da8fe56a66` | `signalsmith-linear/stft.h`, `signalsmith-linear/fft.h`, `LICENSE-LINEAR.txt` |

Upstream sources:
- https://github.com/Signalsmith-Audio/signalsmith-stretch
- https://github.com/Signalsmith-Audio/linear

The original source headers are unmodified. Integrator controls, DSP lifecycle,
stereo processing, independent tempo, tests and realtime-boundary invariants are
in `engine/player/PlayerPitchTempoProcessor.h` and
`tests/player_pitch_tempo_test.cpp`.

**Product boundary:** This DSP runs only on the existing ArSon Player decoder
worker and sends pre-rendered stereo PCM to the previously verified native
ChannelId mixer. It is not allowed to call from the JUCE device callback.
