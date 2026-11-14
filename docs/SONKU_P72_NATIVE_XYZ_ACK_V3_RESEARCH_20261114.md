# SonKu P7.2 — research-only realtime JUCE XYZ native ACK and v3 state

14 November 2026 · stacked on P7.1 #681 → P7 #679 → P6 #678.
**Research native host feature, not public 3D UX / ASIO / dearVR parity.**

## Audited architecture

The existing SonKu `Settings` POD, per-instance native SPSC queue, JUCE
`consumeRealtimeEdits`, completion ACK, and `SonKuSpatialCore` are the
**single** audio/control source of truth; no second processor, WebAudio DSP,
worker thread, new audio callback, or hidden FX identity.

When **both** the project option
`ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT=ON` (default OFF) and CMake's
specific source targets are built, the JUCE native processor prepares
`SixSurfaceKemarResearch`, compiles the independent MIT KEMAR 3D
measured bank, and extends the existing SonKu POD control with the
research-only `directElevationDegrees` field. This parameter is
**validated in combination with the same ROOM/distance settings**, accepted
by the native callback as one transaction, and included in completion ACK
only after audio applies it. The direct HRIR and six physical reflected
paths slew on existing geometry clocks; no callback allocation or
`prepare()`/history reset during a live edit. 3D edits are rejected if
they violate room clearance or cannot fit the delay ring, preventing
silent clamping of impossible geometry or partial UX state.

The experimental native FX state adds a single float to the existing
SONK v2 payload, making **SONK v3 / 50 bytes**. The exact older binary
v2 / 46-byte and v1 / 30-byte states are still accepted in the
experimental JUCE adapter, defaulting direct elevation to 0°. A
normal/public SonKu binary **still writes v2 46 bytes**, and does not
silently attempt to interpret experimental v3. V3 is not automatically
published in the `.smix` GUI or FX rack; an approved user-facing state/
UI/host protocol migration requires separate evidence and release.

This pilot is also protected against an old-engine bug where a
`setSettings` failure could change the orbit phase before declining
a ROOM edit. A rejected P7.2 edit must preserve accepted state and
azimuth. Device `prepareToPlay(44100/48000, frames)` reuses the
approved elevation. Compiling with the switch OFF emits **none** of the
extra KEMAR 3D tables and retains original v1/v2 persistence.

## Test and release gates

- Linux no-JUCE source CTests: 44.1/48k, 0→+25→−25° live edit on the same
  core; 64/257 chunk parity, moving azimuth continuity and anti-phase
  musical PCM, finite/headroom, no invalid state or rejected-phase movement.
- Real MSVC JUCE adapter: queue returns QUEUED, processBlock applies,
  completion readback is authoritative, rejected geometry never ACKs,
  experimental 50-byte v3 and 46/30-byte v2/v1 restorable, corrupt/truncated
  state fails without accepting partial data, driver-rate reprepare works.
- All tests target only specific native source binaries with pinned
  JUCE 8.0.15 and WebView2 configure headers; no full mixer/installer,
  no Vocal Harmony CI, no heavy UI snapshot jobs.
- These are host/native tests, **not a physical ASIO 64/128/256 XRUN test**
  or approved GUI click/end-to-end WIRE route. Actual board/real music
  listening, multiple inserts, licensing and production UI remain blocked
  on Issue #683.
