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
- **Actual operator brushed metal**: the uploaded 740×493 PNG is downsampled to a 600×400 high-quality JPEG (`assets/fx/graphic-eq31/geq31_brushed_metal.jpg`, SHA256 `4b7b5f68f502482f5d1fc1c973a48ae817087104485c71020c5bdfb8f6d1388c`). It is embedded in production `ARMixAssets`, decoded once with `juce::ImageCache` on editor construction, and drawn on the **single shared panel**. This replaces the imperceptible 3px line simulation. The original source image is not altered.
- **Authentic SVG hardware**: the byte-exact owner's `slider_eq_grp.svg` preserves all Track/Text/THUMB children and its own embedded raster `_Image3` shadow. The native UI clone re-enables **the original raster shadow at opacity 0.87**, with a modest local JUCE thumb shadow, retaining vector thumb artwork. There are no per-band background cards/shadow blocks. The recessed outer slot lip **`g1`** now blends grey-metal shades (`#929b9f` to `#858d92`) darker than the body, and the inner slot **`g2`** stays near-black.
- **Endstop geometry, not guesswork**: outer track Y 147.08–1696.67, vector THUMB Y 776.33–1055.50. Thumb shift is asymmetric: at +12 dB **−629.25 SVG units**, at −12 dB **+641.17 SVG units**. Both are derived from actual path bounds, reaching the source slot ends without overshoot/unused travel. Labels +12/0/−12 align to major authored tick coordinates Y 208.75/918.33/1627.92 using the actual Slider height and Viewport Y rather than fixed CSS positions.
- **Readable frequency caps**: 50px pitch (31×50=1550px) and same original Text and Hz groups; number is `Inter 700 / 27px SVG`, unit `Inter 600 / 18px SVG`, displayed as two lines e.g. 250 Hz and 1.25 kHz. Endpoint status dots remain hidden in the render clone, without altering the source SVG.
- **Uncluttered layout**: one aluminium surface; only Flat and LOW/MID/HIGH controls. No input/output gain knobs, no Smile, no footer or scrollbar commentary. Editor defaults to 1610×391, width-resizable 850–1900, and uses horizontal viewport scrolling on narrower screens.
- Chromium screenshot of the source-derived preview is an art-direction reference, **not native JUCE/Windows or ASIO FAT**. The actual JUCE rendered screenshot, hardware/endstop control feel, true Inter availability on Windows, and mixer editor lifetime remain qualification items.
- The only mixer interface authority remains the existing ArSonMix FX Rack and mixer-owned JUCE editor; no extra PCM path or WebView is added.
- The screenshot/HTML design preview is a source-derived Chromium rendering, **not** evidence that the JUCE Windows native build has passed.

## State and safety

The versioned state loader validates magic, version, exact byte length, every per-band value, and any legacy trims **before changing any parameters**. Live audio coefficients use existing realtime-safe smoothing; no allocation, file IO, locks, or JSON parsing is performed on the realtime callback.

## Unified built-in FX header — 20 November 2026

- The owner's authentic 31-slider SVG `Track` paths are **unchanged geometrically**, but the two tick groups are styled on UI clones with strong `#131a1d` stroke and 7.9 SVG units, visible against the original brushed metal. The recessed silver outer chamfer `g1` remains darker than the body, and `g2` stays nearly black.
- The top-right window chrome now matches the other built-in editors: **Preset** (functional JUCE factory list), **FX power/bypass** (mixer-host authenticated readback), and **Close**. Native `PluginEditorWindow` still owns the actual editor HWND; the embedded Close simply hides it, permitting reopen without losing state. No duplicate floating window or WebView.
- Bypass state comes through `GraphicEq31Editor::bindHostPower` bound in the existing `routingFxEditor` host action using retained native/routed insert lease, the exact existing `bypassRetainedNativeFx` / `bypassRetainedRoutedFx` functions and refreshed `retained*FxPower` readback. No local optimistic boolean; global/rack bypass disables the slot action, and an unavailable lease leaves the button disabled.
- Factory preset list offers **Custom, Flat, Vocal Presence, Warm, Speech Clarity**. Selecting a factory preset updates the 31 actual JUCE band parameters; changing a fader returns the selection to Custom. This is **not a false claim of user `.armixfx` support**: Graphic EQ 31 has not yet been admitted into the shared `BuiltInFxUserPresetStore` accepted-DSP snapshot/ACK workflow. Until that is implemented and tested, the native list is explicitly **factory only**, with `.smix` session state still owned by the existing mixer.
- LOW/MID/HIGH jump buttons are removed. The **Flat** reset is in a narrow right-hand utility rail with room for future actual knobs/buttons, while the 31 faders keep their readable 50px pitch and a horizontal scrollbar in narrow editor widths.
- Header buttons use the same compact neutral silver, Inter, thin borders and accessible tooltips. No extraneous status/help/footer text is introduced.

## Verification evidence and release boundary

- Static/pure-C++ checks: `ARMixGraphicEq31CoreTests` / `GraphicEq31FrequencyResponseAndRealtimeStability` (frequency response, invalid gains, stereo isolation, low-rate Nyquist and automation stress). Its 29 explicit `GEQ_CHECK` predicates remain executable in Release (`NDEBUG`) builds, including setup calls that used to disappear inside standard `assert`.
- Source-artwork regression: `GraphicEq31OriginalSvgLayersAndDynamicThumb` validates the owner's byte-exact SVG, original `Track`/`Text`/`THUMB` structure, owner raster shadow + local depth, asymmetric source-measured thumb travel, textured JPEG body and dark g1/g2 slot, hidden endpoint dots, bold Inter Hz/kHz labels, correct ±12/0 ticks, no Smile/footer text, 50px pitch, and absence of both gain trims.
- Shared host registration: `ARMixNativeFxExtensionTests` (catalog, state roundtrip, bad binary reject, PCM finite, published restore reject).
- Follow the current exact-SHA Windows `native` GitHub CI workflow before merge. A CI success proves compilation and automated tests, not physical sound quality.
- **Still requires owner equipment FAT**: audition all 31 faders at representative rates, verify Flat unity (0dB trims), inspect bypass/restore and `.smix`, open/close editor while live ASIO runs, verify CPU/XRUN and confirm headroom/clipping at ±12dB in a complete music mix.

No claim is made that this is a hardware-accurate proprietary dbx transfer function: frequency centres and classic panel controls are matched, while the original JUCE biquad implementation is independently authored.
