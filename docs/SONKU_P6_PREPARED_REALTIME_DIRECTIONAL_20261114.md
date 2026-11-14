# SonKu P6 — prepared realtime directional KEMAR (experimental integration)

2026-11-14 · stacked on PR #674 / P0–P5 · **not production-qualified**

## What was integrated

The P2 directional image-source measured-KEMAR HRIR renderer, P3 exact
zero-spread source fusion and P4 contiguous 140-tap read are now selectable
as **one immutable prepared native render profile**. `SonKuSpatialCore::prepare`
accepts `SonKuReflectionProfile::DirectionalKemar`. The selected profile
reaches the original `SonKuEarlyReflections` that already handles actual
native `spacePreset` ROOM scenes and is called from one JUCE PCM processor.

`SonKuSpatialProcessor::prepareToPlay` uses this profile only when the
**explicit experimental** CMake option is enabled:

```
-DARMIX_SONKU_EXPERIMENTAL_DIRECTIONAL_RT=ON
```

Default = **OFF**. Do not add this to published installers or release workflows
until qualification. No WebAudio worker, duplicate DSP graph, new FX ID,
.session/`.smix` schema, GUI, bridge, control ACK or WIRE modifications.

The profile is admitted *before* the native room is activated. All experimental
offline setters are rejected on that profile so the renderer cannot be silently
downgraded/mutated after prepare. Device restart re-prepares the profile
and restores the accepted native acoustic scene. ROOM ON/OFF and scene distance
edits continue to use the existing smoothed 64-sample image geometry and
10–24ms level/delay transitions; P5 epoch guard prevents old ring audio.
The realtime callback does not allocate, lock, query clock, clear large rings,
load datasets, spawn threads or select a new renderer.

## Evidence

- Native C++ P6 test covers invalid profile fail closed, default unchanged,
  explicit JUCE-path profile selection, active ROOM + moving orbit,
  44.1/48k mixed-polarity music, fixed versus arbitrary PCM chunks,
  strict no-reflection early prefix, measurable distinction from lightweight
  room, restart with an accepted active scene, and immutable profile rejection.
- All previous P0–P5 fixture/PCM/380-frequency and A/B guards remain.
- The SAME native core, native ROOM scene and exact input are benchmarked as
  `SONKU_P6_RT` at 48k/128, spread 0° and 60°, reporting p99 against the
  theoretical block budget; shared Linux runner numbers are informational only.

## Release veto (open)

This is **engine integration**, not proof of a better audible space.
Directional reflections still use horizontal-only MIT KEMAR, four walls, and
may be expensive for many concurrent inserts. Do not auto-promote this
CMake switch, PR, public binary, or claim dearVR parity before:
1. On the actual Windows JUCE/ASIO mixer, measure p95/p99, XRUN and channel count
   at 44.1/48k with 64/128/256 frames, 1 and several SonKu instances, WIRE + VST3.
2. Same-gain, blind headphone A/B on real stereo material, solo voice, anti-phase,
   FRONT/REAR, movement, ROOM on/off and 0°/60° spread.
3. Confirm tonal coloration, perceived outside-head position, distance ordering,
   no click on edit/restart, no audible late-reverb artifact.
4. Reconcile any main-branch changes and run native JUCE Windows compilation
   before a tester build. Keep original P0–P5 PR draft.

Do not use an unstable per-callback CPU clock to dynamically switch HRTF modes;
a global mixer graph budget belongs to the host admission policy, not a guessed
one-size threshold in the audio thread.

## P6 native ROOM headphone artifact

The scoped SonKu fast CI also renders **eight lossless original 48k/16-bit
stereo WAVs**: LIGHT versus DIRECTIONAL prepared mode on FRONT/REAR and
stereo spreads 0°/60°. Both use the exact same real native SQUARE
ROOM controls, output trim, direct HRIR, synthesized phase-conflicted
L/R instruments and 3-second frame clock (no external licensed music).
`tests/sonku_p6_realtime_audio_audit.py` enforces consistent metadata,
the exact no-reflection early prefix, finite / unclipped and non-silent
PCM, and a genuine measured wet-output difference after reflections.
Metrics are uploaded with the WAVs and listening guide. A difference
from lightweight reflections is expected; it is **not** evidence the
candidate sounds better, externalizes more or matches dearVR.

## Native build reproducibility

`ARMixSonKuP6PreparedTests` is now a focused portable CTest target.
The existing real JUCE `ARMixSonKuNativeStateTests` asserts the prepared
profile in **both** option-OFF (Lightweight) and explicit option-ON
(DirectionalKemar) Windows builds, alongside native ACK/state restoration.
Use the exact PR SHA to compile the test once on Windows; Linux native
GitHub Actions proves portable-core behavior but not a Windows JUCE binary.
