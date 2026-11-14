# SonKu P8.3 — Native physical speaker research checkpoint

**Status:** experimental source-mode DSP; do NOT merge into public main.
Base: PR #697 above P8.2 PR #695. Existing P8.2 portable artifact
`14118867259` is the golden headphone reference.

## Code, not merely an ARD

`SonKuP83SpeakerScene.h` defines explicit `MusicBedP82`,
`MonoObject` and `SpeakerMonitor` semantics. The default remains
the former P8.2 Music Bed with its approved bass and SIDE behavior.
Research-only quiescent methods on `SonKuSpatialCore` select a mode,
head yaw and read-only isolated acoustic stems. They **cannot** be invoked
after PCM starts; no native SPSC/ACK, presets or session version is changed.

- **MonoObject** consumes INPUT LEFT only; one world-positioned emitter
  produces both ears. INPUT RIGHT is deliberately ignored. Never infer
  mono from correlations.
- **SpeakerMonitor** uses independent L and R physical sources, default
  positions -30/+30 degrees around the FRONT, preserving two measured
  time-domain HRTF transfers to both ears. The two sources are never
  collapsed into MID nor augmented by a head-locked headphone SIDE bed.
  No direct LF headphone bed is mixed into this mode.
- **World yaw** is a separate control. Direct angle subtracts head yaw;
  room images remain located in the world and their measured ear arrival
  angles subtract yaw. This covers both horizontal and vertical ER.
  It is *deterministic offline yaw*, NOT live tracking or a sensor pipeline.
- The approved P8.1 tonal calibration is retained for P8.3 research
  direct sound; low frequency/musical quality remains an open listening gate.
- P8.2 materials, measured KEMAR, six walls, and 3-band FDN are shared,
  not reimplemented. P8.2 speaker behavior is not rewritten.
- `Stem` Direct/Early/Late plus six-bit wall mask gives exact native
  direct, horizontal, floor, ceiling, late and combined captures. It is
  diagnostic output gating, not arbitrary room gain or elevation hacks.

## Fast reproducible evidence

GitHub Actions `sonku-p83-native-speaker-gates.yml` builds the actual
`SonKuSpatialCore` at 44.1/48 kHz, runs source-direction, yaw, +/-180,
independent two-speaker L/R, correlated, antiphase, partial-coherence,
Music Bed chunk-parity and masked room-path probes. It checks finite
PCM, interaural sign, non-null side-only music, first-arrival ordering,
and native diagnostic stems. A separate unmodified P8.2 defect
reproduction runs alongside to avoid declaring the old failure a PASS.
The artifact offers deterministic 48k float32 stereo WAV stems; **these
WAVs are diagnostic impulses/tones, not natural musical or ASIO FAT**.

Run locally:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic \
  -DSONKU_P7_MEASURED_ELEVATION_RESEARCH=1 \
  -DSONKU_P71_DIRECT_XYZ_RESEARCH=1 -DSONKU_P72_NATIVE_XYZ_RT=1 \
  -DSONKU_P75_MUSIC_SAFE_RESEARCH=1 \
  -DSONKU_P80_TRANSPARENT_BASS_RESEARCH=1 \
  -DSONKU_P82_MULTIBAND_ROOM_RESEARCH=1 \
  -DSONKU_P83_PHYSICAL_SPEAKER_RESEARCH=1 \
  tests/sonku_p83_physical_speaker_source_test.cpp -o /tmp/p83-speaker
/tmp/p83-speaker --wavdir /tmp/p83-research-pcm
```

## Gates NOT passed by this source milestone

1. This is NOT yet user-switchable in the full native JUCE host. Next
   vertical integration adds versioned `.smix` render mode, native
   SPSC/ACK, live speaker audition, portable Windows research candidate
   and a minimal mode selector WITHOUT changing approved visual skins.
2. True head yaw updating *during* callback needs bounded native
   command/queue and smooth kernel transitions. Current quiescent yaw
   validates world-frame geometry, not tracking latency or drift.
3. Linear HRIR interpolation preserves tap timing numerically but phase
   behavior between measured grid points still needs objective ITD/
   spectrum testing and listening for front/back/elevation. Additional
   SOFA banks need explicit licensing.
4. No per-device ASIO p95/p99/XRUN or 1/2/4 insert live profiling here.
5. Speaker mode anti-phase non-null is necessary but insufficient:
   measure LF, narrow-band comb depth, vocals and stereo against P8.2.
   Do not approve without level-matched music and blind headphone
   directional/externalization evidence (including overhead errors).
6. Acoustic source and head yaw are world-separated; actual BRIR/headphone
   calibration and floor/ceiling contribution require human comparison.
7. Never claim dearVR PRO 2 parity without matched hardware and listener A/B.

## Further source evidence

The P8.2 default is now compared bytewise across two independent native
compilations, with P8.3 macro disabled and enabled but unselected; both
ROOM ON/OFF and correlated/antiphase inputs at 44.1/48 kHz. Research
impulse measurements include full-window IACC/ITD, +/-20 degrees direct
elevation, spectral level and ear phase at 83, 461, 1703 and 5387 Hz,
and reconstruction of isolated native wall/late stems. **IACC is not
perceptual externalization**. CPU p50/p95/p99 and ASIO measurements
remain a distinct performance milestone, not proven by this source test.
