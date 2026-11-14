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

## P8.3 experimental music-integrity rejection (14 November 2026)

48k measured native speaker anti-phase drops 83 Hz by -25.112 dB
relative to P8.2, but 1703 Hz by only -1.374 dB. The resulting
LF-versus-mid difference is -23.738 dB; a global trim cannot restore
this ratio. At 44.1k the similar values are -26.095/-2.113 dB.
A real symmetric pair of speakers playing opposing polarity can cancel
bass near a central listener. However, this is still a music-fidelity
**failure** against the owner's SonKu contract, NOT a successful candidate.
Keep Music Bed default and do not ship a compensating headphone stereo
bed as a disguised physical speaker. The CI includes an intentional
**red LF quality veto** until a physical and musically acceptable solution
passes. This gate is independent of the native directional source PASS.
A 44.1/48k, 64/128/256, 1/2/4-insert Linux hosted CPU benchmark is
also added; neither this benchmark nor a source PASS proves ASIO XRUNs.

## Opt-in music-compatible LF research mode, not a physical BRIR

The measured physical stereo 2x2 KEMAR transfer remains default and
separately reported. Real symmetric loudspeakers can cancel anti-phase
LF at a central listener. A 150-Hz complementary crossover can
instead substitute ONLY the direct low-frequency transfer with a
source-position/head-yaw-aware 2x2 ear matrix. Both sources feed both
ears, but this low-band compensation intentionally introduces
non-physical ILD and is NOT the measured response of two speakers.
It is research-only and explicit through quiescent
`setResearchSpeakerMusicLowFrequency(true)`; the full measured
upper-frequency HRTF, six directional walls, room materials and FDN
remain intact. MonoObject and P8.2 Music Bed are unchanged.
This is not a hidden headphone SIDE return, but it is a monitored
physical-fidelity tradeoff; user listening must assess whether that
tradeoff is acceptable.

The source gate compares physical versus music LF anti-phase, correlated,
hard-L/R, head-yaw and block parity, and records reproducible WAVs.
The relative -6 dB threshold now checks the opt-in monitoring variant.
A PASS is only a *tonal source gate*, not proof of externalization.

## Narrow-band regression audit

An initial opt-in 150-Hz direct LF crossover improved 48-kHz anti-phase
83 Hz from -25.112 dB to -2.849 dB vs P8.2, but 461 Hz remained
-11.520 dB, and mono 1703 Hz remained -7.207 dB. **Do not claim
music fidelity PASS from the original 83-Hz-only quality gate.**
The native source now tests crossover frequencies 150/220/350/480/650 Hz
on *the same* original input, printing 83/461/1703/5387-Hz
anti-phase and correlated-mono spectral differences for engineering
selection. A larger LF substitution harms physical low-mid ITD/ILD;
select only after reviewing these cross-band measurements.

## Chosen P8.3 opt-in crossover, based on same-source 44.1/48k sweep

The smallest measured crossover in the sweep that avoided a >6dB
reduction across *both* 83 Hz and 461 Hz was **480 Hz**:
- 48k anti-phase: 83 -1.693dB, 461 -5.801dB, 1703 -0.173dB
  and 5387 +0.917dB relative to P8.2.
- 48k correlated mono: 83 +1.053dB, 461 +1.096dB, 1703
  -3.887dB and 5387 -2.781dB.
- 44.1k anti-phase: 83 -1.713dB, 461 -5.665dB,
  1703 -0.792dB, 5387 +0.120dB.
- 44.1k correlated mono: 83 +1.016dB, 461 +1.295dB,
  1703 -3.395dB and 5387 -2.795dB.

650Hz would improve the 461Hz differential further but destroys
more measured low-mid physical cues. 480Hz is only a tradeoff candidate,
NOT a psychoacoustically verified optimum. Source gates now reject
relative dips *or boosts* outside +/-6dB at all four frequencies and
both rates for BOTH correlated and anti-phase tests, while the
unaltered physical mode remains separately measurable. Any unsampled
spectral cancellations, room coloration or headphone externalization
failures still veto product approval.

## Offline headphone A/B with your OWN music, exact native DSP

`tools/sonku_p83_offline_ab.cpp` reads a user-owned uncompressed
stereo WAV (PCM16/24 or IEEE float32, 44.1/48k) and writes
`P82_MusicBed.wav`, `P83_SpeakerPhysical.wav` and
`P83_SpeakerMusicLF480.wav`, with matching input, trim (.75),
speaker angle (-30/+30), head yaw (0), distance (1.6m), timebase and
optional `--room=on` (actual P8.2 three-band FDN
and six-wall early room). It uses the EXACT native SonKuSpatialCore,
not simulated Javascript/WebAudio. Default `--room=off` isolates
direct sound first. No tracker, live host routing or VST3 integration.

Windows MSVC build command (developer toolchain; not a packaged app):
`cl /std:c++20 /O2 /EHsc /DSONKU_P7_MEASURED_ELEVATION_RESEARCH=1
 /DSONKU_P71_DIRECT_XYZ_RESEARCH=1 /DSONKU_P72_NATIVE_XYZ_RT=1
 /DSONKU_P75_MUSIC_SAFE_RESEARCH=1
 /DSONKU_P80_TRANSPARENT_BASS_RESEARCH=1
 /DSONKU_P82_MULTIBAND_ROOM_RESEARCH=1
 /DSONKU_P83_PHYSICAL_SPEAKER_RESEARCH=1
 tools\\sonku_p83_offline_ab.cpp /Fe:sonku-p83-ab.exe`

Run `sonku-p83-ab.exe "D:\\Music\\my-song.wav" "D:\\SonKuAB" --room=off`.
For room comparison repeat with separate output folder and `--room=on`.

The utility records **raw**, NOT matched-loudness output PCM and
`sonku_p83_ab_metrics.csv` (whole-file RMS/peak). Listening FAT
must loudness-match on the same headphones/audio interface and use
one song excerpt; normalize with an external trusted tool, not by
arbitrary output makeup in this renderer. No stream/ASIO/GUI changes,
no public release. Do not use this CLI to claim latency or real
speaker externalization. Only user listening can approve that.

The fast Linux CI compiles and smoke-renders an original test WAV,
uploads three original-source A/B listening files and metrics.
The sample is deliberately original synthesized material, not
proprietary music.

Input support also includes common WAVEFORMATEXTENSIBLE PCM/IEEE float
WAVs from DAWs. WAV PCM32 signed integer is supported; unsupported
sample rates, compressed WAV and mismatched valid-bits fields fail
closed. Output is standard IEEE 32-bit float WAV with a `fact` chunk.
A separate CI smoke exercises extensible float32 input with ROOM OFF.

## Smart CI cost regression corrected

In the active P8.3 PR, three historical P7.4/P7.5/P8.0 Windows FAT
workflows still had `pull_request` path filters on all `sonku/**`.
They each launched a full JUCE Windows portable build on ordinary
P8.3 source/diagnostic pushes, despite the repository's selective FAT
policy. This workstream disables their automatic PR trigger, retaining
explicit `workflow_dispatch` on each historical workflow. The
path-scoped C++ P8.3 Linux source, audio, A/B and CPU gates continue
automatically. New P8.3 JUCE/ASIO FAT must use a deliberate exact-SHA
qualification after native host render-mode/ACK integration and musical
source gates, NOT the old P7 Windows binaries.
