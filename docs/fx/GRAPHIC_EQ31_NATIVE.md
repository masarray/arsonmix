# Graphic EQ 31 — Native built-in FX integration

**Implementation branch:** `feat/graphic-eq31-native-fx`  
**Native catalog:** `armix.builtin.graphic-eq31.v1`  
**Operator name:** **Graphic EQ 31**  
**Delivery:** JUCE native insert under the existing ArSonMix FX Rack, **not** a standalone app, external VST3 or FactoryFX.

## What is owned where

- `engine/builtin_fx/GraphicEq31Core.h` owns the fixed 31-frequency stereo signal path. 31 matched second-order peaking sections are directly cascaded without adding a callback or allocation. Nominal centres are the ISO/dbx-style 1/3-octave series from 20 Hz to 20 kHz; Q is fixed at 4.318473, so adjacent bands overlap.
- `engine/builtin_fx/GraphicEq31Processor.h` adapts to the existing JUCE `AudioProcessor` and `RealtimeNativeFxControl`. Stable per-band `AudioParameterFloat` IDs `band0`…`band30`, plus exactly one `inputDb`, are used for host parameter bindings. No `outputDb` processing or output-gain UI exists in the current version.
- `src/NativeBuiltInFx.h` registers one catalog descriptor. Existing `FxInstance`, per-ChannelId ordering, per-insert bypass, lifecycle, `.smix` binary storage, device switching and generic owned editor remain the exclusive authorities.
- `web/approved-native-bridge.js` adds a discovery fallback until native `arMixCatalog` returns the canonical descriptor. **No second GUI or WebAudio processing** is introduced.
- `assets/fx/graphic-eq31/slider_eq_grp.svg` is the owner's **byte-exact original 33,354-byte SVG** (`SHA256 aeaf02b8904a667724f34774a692da39a5a466bae302611f347fae4d9fd14dba`). Its three authored children `Track`, `Text`, and `THUMB` are preserved. The production JUCE binary embeds the entire SVG including the original raster/vector THUMB. The editor changes only the `Text` frequency tspan while parsing each band **once** on the UI thread; afterwards it holds the original authored `THUMB` Drawable and applies a simple affine Y transform on parameter changes (no XML reparse, PNG decode, or allocation in the drag path). There are **no JUCE-painted substitute tracks or silver thumbs**; invalid SVG displays a visible error. No BinaryData means the graphical layer cannot render, rather than silently showing a fake substitute.

## Frequencies

```text
 20   25   31.5   40    50    63    80   100   125   160   200
 250  315  400    500   630   800   1k   1.25k 1.6k  2k   2.5k
 3.15k 4k  5k     6.3k  8k    10k   12.5k 16k   20k
```

- Per-band boost/cut: **±12 dB**, fixed nominal centres.
- **One Input Gain only**, **±12 dB**, smoothed. The output trim control and its audio multiplier were deleted; peak metering remains post-EQ.
- Automatic sample-rate reprepare and Nyquist guard; a band above the physical rate's guard keeps its saved value but its filter is transparent until supported.
- Linked stereo gain coefficients, separate L/R filter history, no artificial delay or lookahead.
- UI controls: 31 faders, centre-zero double-click, Flat/Smile quick buttons, owner `KNOB-EQ.svg` input gain and clip display. The one approved `/preview/` remains the mixer surface; this effect uses the existing JUCE owned plugin-editor window.
- The editor defaults to **1370×391** and resizes horizontally from **850 to 1800 px**. Its pitch is **37 px per slider** (31×37=1147px), reduced from 80px. At default width all bands fit simultaneously; narrower windows retain horizontal LOW/MID/HIGH navigation. Source `slider_eq_grp.svg` remains byte-exact and retains its authored `Track`/`Text`/`THUMB` groups; the frequency label alone is updated on the UI thread and rendered in Inter.
- The **byte-exact original `KNOB-EQ.svg`** (`SHA256 754b3d525be1d4dcbe26fdca1d6bedc5b67e2d2436efec2aa9265ad86c5aad80`) is embedded alongside the fader. The native editor draws the original `KNOB` layer and rotates the original `POINTER` group around its authored origin. All JUCE/HTML EQ labels/buttons request **Inter**, with no extra output gain control. **On Windows, JUCE needs Inter installed** to avoid automatic system-font fallback; no extra font binaries are bundled in this PR. No second UI host/PCM owner was introduced.
- A source-derived HTML/PNG preview may help inspect the art direction but must be labeled **not a live JUCE screenshot** and must never be used as physical FAT proof.

## State and safety

A separate versioned `GEQ1` **v2 136-byte** binary payload stores 31 band gains and one input gain. **Legacy v1 140-byte** payloads are validated in full and migrated, discarding their deprecated output trim so it cannot secretly alter output PCM. `restoreUnpublishedState` validates the **entire** blob before changing parameters and refuses modifications after the instance has been sealed into a published realtime graph. The existing host prepares a replacement lease rather than mutating published DSP state. The processor exposes only an atomic per-block peak to its GUI.

Coefficient transitions are smoothed over 5ms on the audio thread when gain targets change; parameter values are atomics, not locks. The realtime implementation avoids disk, JSON and heap allocation.

## Verification evidence and release boundary

- Static/pure-C++ checks: `ARMixGraphicEq31CoreTests` / `GraphicEq31FrequencyResponseAndRealtimeStability` (frequency response, invalid gains, stereo isolation, low-rate Nyquist and automation stress). Its 29 explicit `GEQ_CHECK` predicates remain executable in Release (`NDEBUG`) builds, including setup calls that used to disappear inside standard `assert`.
- Source-artwork regression: `GraphicEq31OriginalSvgLayersAndDynamicThumb` validates byte-exact checksums for both owner assets, original `Track`/`Text`/`THUMB` and `KNOB`/`POINTER` groups, CMake BinaryData ownership, compact 37px pitch, Inter, and absence of output trim.
- Shared host registration: `ARMixNativeFxExtensionTests` (catalog, state roundtrip, bad binary reject, PCM finite, published restore reject).
- Follow the current exact-SHA Windows `native` GitHub CI workflow before merge. A CI success proves compilation and automated tests, not physical sound quality.
- **Still requires owner equipment FAT**: audition all 31 faders at representative rates, verify Flat unity (0dB trims), inspect bypass/restore and `.smix`, open/close editor while live ASIO runs, verify CPU/XRUN and confirm headroom/clipping at ±12dB in a complete music mix.

No claim is made that this is a hardware-accurate proprietary dbx transfer function: frequency centres and classic panel controls are matched, while the original JUCE biquad implementation is independently authored.
