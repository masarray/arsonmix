# 8D-SonKu V4 — independent source-distance and large-room candidate
2026-11-13 | offline headphone experiment | stacked on approved Square + V3 FDN PR #629

## Baseline lock / non-goals
The owner approved a 7.2 × 7.2 m Square Orbit at 1.6 m emitter radius.
Checkpoint `3a74b0273c7a7e13d5d2614183d19995eb12ad57` must not move.
Do NOT modify any HRIR coefficients, direction signs, native SonKu controls,
V3 reverb presets, production JUCE state, mixer WIRE routing, or public Pages.

## Why ROOM SIZE and SOURCE DISTANCE are NOT the same parameter
- **Room dimensions:** move virtual image walls, which changes first reflection
  arrival times and amplitude by source-to-listener path distance.
- **Source distance:** changes relative direct energy (reference pressure 1.6/r),
  while each reflected path is independently scaled with 1.6/path. The 1.6 m
  reference is unchanged, including EXACT original floating-point operation order.
- **Diffuse late reverb:** not physically equivalent to either quantity. The
  existing Studio Glue (0.78 s) and Airy Chamber (1.28 s) are deliberately OFF
  in new distance/room A/B tracks. A large "hall" RT60 must be separately
  modeled, calibrated and listened to; do not fake hall size with one longer tail.

## Engine changes and realtime constraints
- `SonKuEarlyReflections::setGeometry({width,depth,distance})` controls an
  unpublished, inactive processor only. Geometry is bounded to 5–40 m per
  dimension, source distance 1.6–10 m, >0.5 m wall clearance and worst image
  path within the existing 8192-frame circular delay ring at 48 kHz.
- First-order image-source paths still use 2 emitters × 4 walls, 64-frame
  geometry cadence, fractional-delay taps, smoothed delays and gains. No ray
  tracing, FFT, new thread, host callback, heap or IO in `process()`.
- `SonKuSpatialCore` applies reference-distance gain ONLY to direct measured
  HRTF when an experimental non-reference geometry is configured; reflections
  use absolute reflected path pressure and the existing FDN stage is unchanged.
- This is NOT a full physical BRIR: no floor/ceiling, higher-order reflections,
  occlusion, calibrated high-frequency air absorption, direct time-of-flight,
  personalized pinna model, dynamic head tracking or release-qualified tail.
- No GUI/bridge/lease/session edits; ALL production instances remain legacy
  geometry with direct gain=1 and the approved PCM arithmetic unchanged.

## Matched-source audition matrix (two 10-second 360° revolutions)
The existing `sonku-v2-square-orbit-cw-2x.wav` remains the reference.

| WAV stem | Width × depth | Emission radius | Late reverb | Intended comparison |
| --- | --- | --- | --- | --- |
| v2-square | 7.2 × 7.2 m | 1.6 m | OFF | Owner-approved |
| v4-spacious-near | 14.4 × 14.4 m | 1.6 m | OFF | Room dimension only |
| v4-spacious-far | 14.4 × 14.4 m | 5.0 m | OFF | Distance only vs spacious-near |
| v4-large-near | 28.8 × 28.8 m | 1.6 m | OFF | Larger room without moving source |
| v4-large-far | 28.8 × 28.8 m | 8.0 m | OFF | Distance only vs large-near |

All render the same deterministic noise, source stereo, azimuth path,
KEMAR HRIR, wet setting and trim. Files are NOT normalized: distant samples
are truly quieter. For localization evaluation match perceived monitoring
loudness safely and separately evaluate the physical level change.
Headphones only; disable OS spatial enhancement.

## Gates
1. Legacy default Square vs explicit identical geometry: byte-exact early
   reflection and full native-core PCM under both 44.1/48k.
2. Larger room first-echo onset must move later, beyond the reference.
3. Distant direct field must approximate 1.6/r with ER OFF; reject invalid,
   nonfinite, too-small, out-of-wall geometry and active edits.
4. Complete WAV formats, continuous 2-turn seam, non-silent tails,
   direction signs, parity, amplitude safety and same-runner CPU telemetry.
5. Listener A/B judgment and Windows ASIO hardware tests are separate,
   necessary release gates. No native GUI control or installer promotion yet.

## Research
- Allen and Berkley, JASA 65(4), 943–950 (1979), DOI 10.1121/1.382599:
  first-order image-source paths for rectangular rooms.
- Valve Steam Audio core guide, direct distance attenuation and reflection
  model as separate signal-processing stages:
  https://github.com/ValveSoftware/steam-audio/blob/master/core/doc/guide.rst

No external code or proprietary impulse responses copied.
