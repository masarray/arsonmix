# Graphic EQ 31 — Native built-in FX integration

**Implementation branch:** `feat/graphic-eq31-native-fx`  
**Native catalog:** `armix.builtin.graphic-eq31.v1`  
**Operator name:** **Graphic EQ 31**  
**Delivery:** JUCE native insert under the existing ArSonMix FX Rack, **not** a standalone app, external VST3 or FactoryFX.

## What is owned where

- `engine/builtin_fx/GraphicEq31Core.h` owns the fixed 31-frequency stereo signal path. 31 matched second-order peaking sections are directly cascaded without adding a callback or allocation. Nominal centres are the ISO/dbx-style 1/3-octave series from 20 Hz to 20 kHz; Q is fixed at 4.318473, so adjacent bands overlap.
- `engine/builtin_fx/GraphicEq31Processor.h` adapts to the existing JUCE `AudioProcessor` and `RealtimeNativeFxControl`. Stable per-band `AudioParameterFloat` IDs `band0`…`band30`, plus `inputDb` and `outputDb`, are used for the host's existing native plugin-editor parameter bindings.
- `src/NativeBuiltInFx.h` registers one catalog descriptor. Existing `FxInstance`, per-ChannelId ordering, per-insert bypass, lifecycle, `.smix` binary storage, device switching and generic owned editor remain the exclusive authorities.
- `web/approved-native-bridge.js` adds a discovery fallback until native `arMixCatalog` returns the canonical descriptor. **No second GUI or WebAudio processing** is introduced.
- `assets/fx/graphic-eq31/slider_eq_vector.svgz` contains the user's supplied fader's native SVG paths, converted to a **track-only** asset by removing the fixed thumb and embedded raster/label. On production GUI builds the asset is decompressed once on the UI thread; each slider draws the actual original vector track with a dynamic JUCE thumb, allowing separate per-band movement. Native no-BinaryData unit-test builds use a procedural fallback.

## Frequencies

```text
 20   25   31.5   40    50    63    80   100   125   160   200
 250  315  400    500   630   800   1k   1.25k 1.6k  2k   2.5k
 3.15k 4k  5k     6.3k  8k    10k   12.5k 16k   20k
```

- Per-band boost/cut: **±12 dB**, fixed nominal centres.
- Input/output trim: **±12 dB**, smoothed.
- Automatic sample-rate reprepare and Nyquist guard; a band above the physical rate's guard keeps its saved value but its filter is transparent until supported.
- Linked stereo gain coefficients, separate L/R filter history, no artificial delay or lookahead.
- UI controls: 31 faders, centre-zero double-click, Flat/Smile quick buttons, input/output gain and clip display. The one approved `/preview/` remains the mixer surface; this effect uses the existing JUCE owned plugin-editor window.
- The native editor defaults to **1280×328** and may be resized horizontally from **1070 to 1700 px**. Frequency labels and all 31 faders use a single width-derived column layout; the moving metal thumb is clipped by its column width to prevent overlaps. The operator's SVG track is embedded only in the production JUCE binary; independent CTest targets use the procedural track fallback.
- A source-derived HTML/PNG preview may help inspect the art direction but must be labeled **not a live JUCE screenshot** and must never be used as physical FAT proof.

## State and safety

A separate versioned `GEQ1` 140-byte binary payload stores 31 gains and 2 trims. `restoreUnpublishedState` validates the **entire** blob before changing parameters and refuses modifications after the instance has been sealed into a published realtime graph. The existing host prepares a replacement lease rather than mutating published DSP state. The processor exposes only an atomic per-block peak to its GUI.

Coefficient transitions are smoothed over 5ms on the audio thread when gain targets change; parameter values are atomics, not locks. The realtime implementation avoids disk, JSON and heap allocation.

## Verification evidence and release boundary

- Static/pure-C++ checks: `ARMixGraphicEq31CoreTests` / `GraphicEq31FrequencyResponseAndRealtimeStability` (frequency response, invalid gains, stereo isolation, low-rate Nyquist and automation stress). Its 29 explicit `GEQ_CHECK` predicates remain executable in Release (`NDEBUG`) builds, including setup calls that used to disappear inside standard `assert`.
- Shared host registration: `ARMixNativeFxExtensionTests` (catalog, state roundtrip, bad binary reject, PCM finite, published restore reject).
- Follow the current exact-SHA Windows `native` GitHub CI workflow before merge. A CI success proves compilation and automated tests, not physical sound quality.
- **Still requires owner equipment FAT**: audition all 31 faders at representative rates, verify Flat unity (0dB trims), inspect bypass/restore and `.smix`, open/close editor while live ASIO runs, verify CPU/XRUN and confirm headroom/clipping at ±12dB in a complete music mix.

No claim is made that this is a hardware-accurate proprietary dbx transfer function: frequency centres and classic panel controls are matched, while the original JUCE biquad implementation is independently authored.
