# SonKu P7.3 — native GUI/bridge contract research milestone
Date: 14 November 2026 | stacked P7.3 → #685 (P7.2) → #681 (P7.1)

## Scope / actual implementation

The existing `src/Main.cpp` SonKu native action dispatcher now delegates both
typed incoming `nativeSonKuSet.settings` and outbound accepted
`nativeSonKuSnapshot.settings` to **one**
`src/SonKuNativeBridgeContract.h`, rather than maintaining a divergent
manual v2 parse and snapshot. This is a CONTROL-thread-only codec and
contains no second audio renderer or graph. It checks finite typed numbers,
native `Settings::valid()`, bool types, geometric admissibility and all
mandatory v1 fields. Optional legacy v2 ROOM fields remain backwards
compatible. The native SPSC callback and native applied ACK remain the
sole authoritative audio state.

- **Production default:** `ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT=OFF`
  means `schemaVersion=2`, the bridge snapshot has no elevation, v2-only
  settings **reject** unexpected `directElevationDegrees`; the normal
  46-byte SONK v2 FX state persists unchanged.
- **Experimental:** option ON means `schemaVersion=3`, the same typed
  settings codec admits measured `directElevationDegrees` jointly with
  radial distance/ROOM. Missing elevation from old browser clients defaults
  exactly 0°. P7.2 native FX persists v3 50 bytes and can restore v1/v2.
  No second `.smix` document schema or unverified host path introduced.

The existing SonKu editor (same `/preview-v2/` asset, no cloned UI) shows
a single compact **SOURCE ELEVATION** slider *only after* an exact
ChannelId/instance/deviceEpoch/graphRevision matched native snapshot
advertises v3 with valid elevation. Range is derived from actual source
radial distance and room height, respecting ear clearance and measured
bank angle; room/distance changes transparently fit existing elevation
within the new room. The main 2D top-down SVG is still the azimuth
control; elevation is one independent, keyboard-accessible source height
control. The stage caption changes from 0° to measured 3D only when
native capability is established.

All gestures continue to use pre-existing 25Hz bounded SPSC intent,
latest-wins coalescing, and **ACK + snapshot appliedRequestId**
verification before claiming DSP APPLIED. The new slider is not a
WebAudio panner or a speculative optimistic DSP state. A stale graph
revision/device epoch snapshot cannot arm v3 capability. `hidden`
overrides the existing grid display rule so **public v2 GUI size and
layout are unchanged**.

## Evidence / non-claims

- C++ native Windows CTests compile and run v2/v3 actual typed JSON codec,
  including invalid elevation, NaN, incompatible room distance, wrong types,
  and fail-atomic decode. P7.2 real JUCE PCM callback/ACK and v1/v2/v3 state
  tests are reused; no entire installer/full-application rebuild.
- Linux Node checks actual SonKu GUI script syntax and physical room
  geometry (old v2, source height, fit), plus native GUI bridge source
  regression. P0–P7.2 PCM negative controls remain in scoped CI.
- Still **NOT proven:** full `ARMixConsole` Windows integration build,
  physical ASIO + WIRE/VST3, human HRTF/dearVR listening, published
  `.smix` end-to-end click/open/save/restore in a native tester, release
  licensing, or P7.3 interaction design certification. Keep all stacked PRs
  DRAFT and the production XYZ feature OFF until these gates are proven.
