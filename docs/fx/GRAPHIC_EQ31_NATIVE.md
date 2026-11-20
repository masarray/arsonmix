# Graphic EQ 31 — Native built-in FX integration

**Implementation branch:** `feat/graphic-eq31-native-fx`  
**Native catalog:** `armix.builtin.graphic-eq31.v1`  
**Operator name:** **Graphic EQ 31**  
**Delivery:** JUCE native insert under the existing ArSonMix FX Rack, **not** a standalone app, external VST3 or FactoryFX.

## What is owned where

- `engine/builtin_fx/GraphicEq31Core.h` owns the fixed 31-frequency stereo signal path. 31 matched second-order peaking sections are directly cascaded without adding a callback or allocation. Nominal centres are the ISO/dbx-style 1/3-octave series from 20 Hz to 20 kHz; Q is fixed at 4.318473, so adjacent bands overlap.
- `engine/builtin_fx/GraphicEq31Processor.h` is a native JUCE `AudioProcessor`, controlled by `band0`…`band30` only. The input/output trim parameters were fully removed from DSP and editor; legacy saved states migrate safely.
- `src/NativeBuiltInFx.h` registers one catalog descriptor. Existing `FxInstance`, per-ChannelId ordering, per-insert bypass, lifecycle, `.smix` binary storage, device switching and generic owned editor remain the exclusive authorities.
- `web/approved-native-bridge.js` adds a discovery fallback until native `arMixCatalog` returns the canonical descriptor. **No second GUI or WebAudio processing** is introduced.
- `assets/fx/graphic-eq31/slider_eq_grp.svg` is the owner's **byte-exact original 33,354-byte SVG** (`SHA256 aeaf02b8904a667724f34774a692da39a5a466bae302611f347fae4d9fd14dba`). Its three authored children `Track`, `Text`, and `THUMB` are preserved. The production JUCE binary embeds the entire SVG including the original raster/vector THUMB. The editor changes only the `Text` frequency tspan while parsing each band **once** on the UI thread; afterwards it holds the original authored `THUMB` Drawable and applies a simple affine Y transform on parameter changes (no XML reparse, PNG decode, or allocation in the drag path). There are **no JUCE-painted substitute tracks or silver thumbs**; invalid SVG displays a visible error. No BinaryData means the graphical layer cannot render, rather than silently showing a fake substitute.

## Frequencies

```text
 20   25   31.5   40    50    63    80   100   125   160   200
 250  315  400    500   630   800   1k   1.25k 1.6k  2k   2.5k
 3.15k 4k  5k     6.3k  8k    10k   12.5k 16k   20k
```

- 31 fixed dbx/ISO nominal one-third-octave centres, each **±12 dB**, linked stereo gains with independent L/R filter history. Zero additional delay/lookahead and normal host bypass.
- **No input or output gain control** and no hidden input/output gain multiplication. Flat with zero bands is unity; peak metering is post-EQ.
- **GEQ1 v3** state stores 31 bands only (**132 bytes**). Historic **v1 140-byte** and **v2 136-byte** states are validated entirely and imported, discarding obsolete input/output trim values. Invalid state fails closed; published graph state is not mutated.
- GUI: authentic owner `slider_eq_grp.svg` remains byte-identical (original `Track`, `Text`, `THUMB` groups). The JUCE editor creates 31 independent Drawables once on the UI thread and moves each vector `THUMB` by Y translation. The duplicate raster use `_Image3` (dark rectangular shadow) is **omitted from the render clone**, not deleted from the source. The duplicate per-band `Hz` group is also hidden in each render clone to declutter the UI. Frequency labels remain within the original `Text` group, switched to Inter.
- **50 px pitch** (31 × 50 = 1550 px) rather than the unreadable previous 37 px. The editor defaults to **1610 × 391 px** and can be resized horizontally from **850 to 1900 px**; smaller windows scroll horizontally with LOW/MID/HIGH jump controls. The source `viewBox` stays unchanged; only the UI clone is cropped horizontally (`60 0 352 2042`) to use screen real estate efficiently.
- One uninterrupted neutral EQ background. **No per-fader shadow blocks, additional cards, gain knob, knob asset in production binary, or second GUI host.** `KNOB-EQ.svg` may remain archived in the repository but is no longer included in CMake BinaryData.
- The only mixer interface authority remains the existing ArSonMix FX Rack and mixer-owned JUCE editor; no extra PCM path or WebView is added.
- The screenshot/HTML design preview is a source-derived Chromium rendering, **not** evidence that the JUCE Windows native build has passed.

## State and safety

The versioned state loader validates magic, version, exact byte length, every per-band value, and any legacy trims **before changing any parameters**. Live audio coefficients use existing realtime-safe smoothing; no allocation, file IO, locks, or JSON parsing is performed on the realtime callback.

## Verification evidence and release boundary

- Static/pure-C++ checks: `ARMixGraphicEq31CoreTests` / `GraphicEq31FrequencyResponseAndRealtimeStability` (frequency response, invalid gains, stereo isolation, low-rate Nyquist and automation stress). Its 29 explicit `GEQ_CHECK` predicates remain executable in Release (`NDEBUG`) builds, including setup calls that used to disappear inside standard `assert`.
- Source-artwork regression: `GraphicEq31OriginalSvgLayersAndDynamicThumb` validates the owner's byte-exact SVG, original `Track`/`Text`/`THUMB` structure, shadow removal on the UI clone, CMake BinaryData, 50px fader pitch, Inter, and absence of both gain trims.
- Shared host registration: `ARMixNativeFxExtensionTests` (catalog, state roundtrip, bad binary reject, PCM finite, published restore reject).
- Follow the current exact-SHA Windows `native` GitHub CI workflow before merge. A CI success proves compilation and automated tests, not physical sound quality.
- **Still requires owner equipment FAT**: audition all 31 faders at representative rates, verify Flat unity (0dB trims), inspect bypass/restore and `.smix`, open/close editor while live ASIO runs, verify CPU/XRUN and confirm headroom/clipping at ±12dB in a complete music mix.

No claim is made that this is a hardware-accurate proprietary dbx transfer function: frequency centres and classic panel controls are matched, while the original JUCE biquad implementation is independently authored.
