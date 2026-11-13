# SonKu M4 — Native SPACE / musical ambience release candidate
2026-11-13 | Draft stacked on #643 → #629 → #606 | NOT public release

## Why this is a vertical product slice
Operator opens **8D-SonKu** from the real FX rack and selects OFF / SQUARE /
CHAMBER / LARGE. Two small sliders control source distance and musical
AMBIENCE. A single `nativeSonKuSet` message flows through the existing
device-epoch, graph-revision, exact ChannelId, instance lease, native queue,
JUCE processing callback and completion. The GUI only displays APPLIED after
matching callback ACK and accepted DSP readback. No WebAudio, browser PCM,
fake owner or duplicated audio callback.

Room map:
- OFF: original direct KEMAR HRIR; no ER or late FDN work, v1-compatible.
- SQUARE: 7.2 × 7.2 m, max radius 3.1 m, short Studio Glue decay ~0.78 s.
- CHAMBER: 14.4 × 14.4 m, max radius 6.7 m, Airy decay ~1.28 s.
- LARGE: 28.8 × 28.8 m, max radius 10 m, restrained decay ~2.05 s.
RoomAmount is fixed to the previously tested 0.55 when user first enables
a scene. For each scene, the direct path uses 1.6 / sourceDistance pressure,
the eight first-order wall paths use 1.6 / reflectedPath pressure, and the
diffuse tail is wet-only. The exact approved HRIR azimuth and orbit geometry
are unchanged.

## Realtime invariants
- All 8× per-line FDN feedback gains and damping coefficients for these
  presets are computed on `prepare()` and selected without exp(), new/delete,
  locks, device restart, or blocking calls in the callback.
- Coefficients and wet levels slew on the sample clock; existing ER fractional
  delays and gains slew too, with geometry recalculated only every 64 samples.
- Reenable after disabled room uses a validity horizon on the existing fixed
  8192-sample ER rings; reenable on FDN uses a horizon on the existing 4096-
  frame rings. No ~64/128 KiB buffer clears in a live edit.
- FX graph owns bypass/retire/reorder/routing; SonKu never installs its own
  audio device or hidden monitoring gate.
- Multiple instances have independent fixed memory; no static mutable DSP.

## Persistence and backward compatibility
`SonKuSpatialProcessor` serializes SONK version 2, exactly 46 bytes with
old 30-byte v1 restore supported. New state carries `spacePreset`,
`sourceDistanceMeters`, `roomAmount`, `ambience`. Legacy v1 restores as
room OFF; malformed v2 is rejected atomically; the last accepted edit is
only persisted after audio-applied completion. Accepted snapshot schema=2
returns exactly the stored state. Existing GUI transport v1 fields may omit
new controls and default OFF.

## What still blocks public release
1. CI: source contract, native no-JUCE PCM, Chromium actual WebView2-assembled
   controls, exact-SHA Windows MSVC/CTest, multi-instance restore and restart.
2. Confirm no clicks/glitches on OFF→LARGE→SQUARE, rapid slider changes, rack
   bypass, graph remove, save/open .smix, device swap, and stereo-only policy.
3. Human headphone A/B: compare approved Square reference, Studio Glue, Airy
   and M4 scene presets with music + voice (front/rear distance, flutter,
   metallic coloration, decay masking); adjust only after evidence.
4. Reconcile parent #606 (latest main) → #629 → #643 → this M4 without
   dropping independent WIRE, GUI, updater and FX progress. Do not promote
   public release based on Linux offline/Chromium mocks alone.

## Explicit physical limits
2D first-order 4-wall image model; no height/floor/ceiling, personalized
HRTF, frequency-calibrated air absorption, true measured BRIR, later
higher-order bounces or physically fitted RT60. LARGE is a musical scene
preset, not a certified geometry-based hall reverb. Accurate propagation
delay of the direct path is not modeled, for low latency.

## References (concepts only)
- Allen–Berkley image-source method: JASA 65(4), 1979, DOI 10.1121/1.382599
- Valve Steam Audio core guide: direct distance and room reflections are
  separate stages; no vendor code copied.
  https://github.com/ValveSoftware/steam-audio/blob/master/core/doc/guide.rst
- JUCE AudioProcessor: processBlock realtime cautions and bypass semantics.
  https://docs.juce.com/master/classjuce_1_1AudioProcessor.html
