# ArSonMix VU uniformity and stereo UI — 2026-10-27

Channels 1–6 remain mono (one 14px fixed-color LED bar); channels 7–10 show separate left/right meters; MASTER retains two left/right meters. All 16 meters share the same reusable `meterUnit` renderer, case width 14px, 10px fill, cap, peak, palette, and clip reset behavior. The extra meters reuse existing `updateMeterVisual` and LED quantization without additional timers or per-channel DOM lookup in the hot path.

The MASTER mode BTN-180 MONO text stays black through active/hover state; STEREO remains blue/default.

**Engine boundary:** in browser demo, stereo L/R values are separate simulated visualization only. Existing JUCE native path is a single live stereo input/FX graph, not ten independent audio buses. Only channel 1 and MASTER have actual native meter telemetry; channels 7–10 stay unlit in native until independent routed per-strip stereo telemetry is implemented. Do not substitute synthetic data into the native host or imply multi-bus audio routing is already available.

The 10-strip layout, master width, real master mono fold-down, per-clip click reset and all audio-engine C++ code are unchanged. Legacy session snapshots are loaded with the fixed channel topology (1–6 mono / 7–10 stereo) regardless of older data. `stereo` is included in saved session JSON so future versions can understand visual topology.

Test with `python tests/ui_smoke.py` and `python scripts/build_ui_css.py --check`; native audio builds remain gated by existing Windows CI.
