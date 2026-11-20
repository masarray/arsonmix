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
- **Endstop geometry, aligned to the white index line**: the owner's vector `s11` ridge spans Y 913–920 (centre **916.5**); main +12/0/−12 tick paths sit at Y **208.75 / 918.33 / 1627.92**. The former ±629/641 movement aligned the **thumb body** but stopped the white index short of 12 dB. The corrected movement uses +12 **−707.75** and −12 **+711.42 SVG units** so the white indicator reaches both tick paths. The original SVG artwork is unchanged. At both 286px historical and the new compact 229px rendered slider heights, the 7-unit SVG ridge collapses below 1px and anti-aliases inconsistently; a pixel-snapped, screen-space 2px white line is overlaid in the same authored `s11` position on each of the 31 black thumbs.
- **Readable compact frequency caps**: 40px pitch (31×40=1240px, 80% of the prior 50px) and same original Text and Hz groups; number is `Inter 700 / 27px SVG`, unit `Inter 600 / 18px SVG`, displayed as two lines e.g. 250 Hz and 1.25 kHz. Endpoint status dots remain hidden in the render clone, without altering the source SVG.
- **Compact aluminium layout**: 31 authentic SVG sliders at 80% of their former rendered size, not altered hardware artwork. Default editor **1330×305** (resizable 760–1660 wide), track **40×229px**, viewport **Y=55**—only 2px beneath the Y=53 header divider—and **12px lower margin** below the 238px viewport. Left/right scales use larger bold 12.5px Inter numerals at source tick Y=208.75/918.33/1627.92. Both side labels travel with the view's tick geometry; there are no input/output gain knobs, Smile, LOW/MID/HIGH jump buttons, footer text or second UI.
- Chromium screenshot of the source-derived preview is an art-direction reference, **not native JUCE/Windows or ASIO FAT**. The actual JUCE rendered screenshot, hardware/endstop control feel, true Inter availability on Windows, and mixer editor lifetime remain qualification items.
- The only mixer interface authority remains the existing ArSonMix FX Rack and mixer-owned JUCE editor; no extra PCM path or WebView is added.
- The screenshot/HTML design preview is a source-derived Chromium rendering, **not** evidence that the JUCE Windows native build has passed.

## State and safety

The versioned state loader validates magic, version, exact byte length, every per-band value, and any legacy trims **before changing any parameters**. Live audio coefficients use existing realtime-safe smoothing; no allocation, file IO, locks, or JSON parsing is performed on the realtime callback.

## Unified built-in FX header — 20 November 2026

- The owner's authentic 31-slider SVG `Track` paths are **unchanged geometrically**, but the two tick groups are styled on UI clones with strong `#131a1d` stroke and 7.9 SVG units, visible against the original brushed metal. The recessed silver outer chamfer `g1` remains darker than the body, and `g2` stays nearly black.
- The top-right window chrome now matches the other built-in editors: **Preset** (functional JUCE factory list), **FX power/bypass** (mixer-host authenticated readback), and **Close**. Native `PluginEditorWindow` still owns the actual editor HWND; the embedded Close simply hides it, permitting reopen without losing state. No duplicate floating window or WebView.
- Bypass state comes through `GraphicEq31Editor::bindHostPower` bound in the existing `routingFxEditor` host action using retained native/routed insert lease, the exact existing `bypassRetainedNativeFx` / `bypassRetainedRoutedFx` functions and refreshed `retained*FxPower` readback. No local optimistic boolean; global/rack bypass disables the slot action, and an unavailable lease leaves the button disabled.
- The shared preset list supports **Custom, Flat, Vocal Presence, Warm, Speech Clarity** and named **My Presets**. `Save` opens the native name dialog; the editor delegates listing, typed accepted-state snapshot, atomic .armixfx write and UUID identity to the **existing mixer-owned `BuiltInFxUserPresetStore`** through the retained FX lease. GEQ31 now has a strict 31-finite-band schema (±12 dB, no hidden output/input trim) and a `BuiltInFxAcceptedSnapshot` adapter. On selection, the host reads and validates the entire user file before applying all 31 JUCE parameters, then checks target readback. A small even/odd preset generation prevents the audio callback from processing partial 31-band writes. This control-thread readback **is not** an independent physical sound/ASIO FAT.
- LOW/MID/HIGH jump buttons and the former unused right rail are removed. The **Flat** reset and **Save** are in the header alongside a compact **152×24** preset selector, 24px-high power and the canonical 24px Close. All controls have a common vertical baseline (Y=16) and Inter fonts; the 31 faders now use 40px pitch (80%), with horizontal scrolling only when needed. Header placements and all five 24px control heights are byte-identical to the prior approved version.
- The preset service is **shared and catalog-scoped**, persisted under `BuiltInFxPresets/graphic-eq31` as checksummed `.armixfx` (not a parallel JSON cache). Reopen enumerates the same files. Saving/recalling requires a valid retained mixer FX lease, ready DSP, permitted control state and validated input. The host owns errors/notices; no UI or filesystem operation runs inside the realtime audio callback.

## Owner visual refinement — satin titanium, black thumb and shared Close (20 Nov 2026)

- Source `slider_eq_grp.svg` is byte-identical to the owner file; the four THUMB gradients `g3`–`g6` are recoloured **only in the UI clone** to black anodized metal with graphite face highlights and a restrained silver center index. The original `_Image3` thumb shadow and measured ±12 dB travel remain intact.
- The earlier real brushed-metal JPEG is still embedded unchanged; one 512×256 RGB **satin titanium texture is generated once at editor construction** using only 8% of the source's broad colour/luminance variation plus fine directional grain and subtle specular sheen. Paint only draws this cached image; no live per-fader texture generation, audio-thread work or second DSP path.
- The `geq31.close` JUCE header button now mirrors the approved `site/preview-v2/assets/mixer-contextual-buttons.css` `.armix-builtin-fx-close` surface: **24×24 px**, 4px radius, navy gradient `#3e5866`→`#293e4b`, border `#607d8c`, white **22px** icon using the exact shared SVG X polygon, dark-red hover and stronger red on press. No text glyph substitution. It still hides the retained host-owned editor rather than removing FX.
- Chromium preview with the original 31 vector faders verified that the black thumb remains readable and the Close/Flat/preset interactions work. **Browser-only screenshot is not native JUCE/Windows or hardware proof.** Exact-head Windows MSVC/CTest must pass before merge.

## UX compactness / hardware 0 dB detent — 20 November 2026

- **Strong center detent:** a specialized `GraphicEq31DetentSlider` overrides JUCE's documented `snapValue(double,DragMode)`. While pointer-dragging a fader, entering within **±0.45 dB** snaps to exactly 0.0 dB; while engaged, the fader holds 0 until the attempted drag exceeds **±0.85 dB**. This hysteresis feels like a hardware center notch instead of a jittery rounding zone. At mouseDown the detent is initialized from the *actual current* value, so preset recall, session restore and host parameter readback remain authoritative. No DSP quantization, no new audio callback and no allocation in the audio engine.
- **Scaled graphic only:** all 31 custom SVG track/thumb frames render at **40×229** rather than 50×286; the canonical header icon, preset list, Save, Flat, Bypass, Close layout and sizes stay unchanged. One compact 1330×305 floating editor; when narrow, horizontal scrolling preserves 40px track pitch and both visible scales. Original source `slider_eq_grp.svg` remains byte-identical.
- **Larger symmetric scales:** both left and right edges render dark `12.5px` bold Inter `+12 / 0 / −12`, with identical y positions computed from the authored main SVG ticks. Left edge 45px inset from first band; right inset 45px beyond the last visible band. Header divider Y=53; viewport starts Y=55; retained bottom inset 12px.
- **Qualification:** Chromium source-derived visual preview and pointer tests can check dimensions/detent geometry, but they do **not** qualify native JUCE, actual ASIO rendering or hardware feel. Exact-head Windows MSVC/CTest is required before merging.

## Verification evidence and release boundary

- Static/pure-C++ checks: `ARMixGraphicEq31CoreTests` / `GraphicEq31FrequencyResponseAndRealtimeStability` (frequency response, invalid gains, stereo isolation, low-rate Nyquist and automation stress). Its 29 explicit `GEQ_CHECK` predicates remain executable in Release (`NDEBUG`) builds, including setup calls that used to disappear inside standard `assert`.
- Source-artwork regression: `GraphicEq31OriginalSvgLayersAndDynamicThumb` validates the owner's byte-exact SVG, original `Track`/`Text`/`THUMB` structure, owner raster shadow + local depth, asymmetric source-measured thumb travel, textured JPEG body and dark g1/g2 slot, hidden endpoint dots, bold Inter Hz/kHz labels, dual-side larger ±12/0 scales, real 0dB drag detent, 80%-scale 40px pitch, compact margins, preserved header and absence of both gain trims.
- Shared host registration: `ARMixNativeFxExtensionTests` (catalog, state roundtrip, bad binary reject, PCM finite, published restore reject).
- The white indicator/±12 travel and the new typed .armixfx save/list/recall path are checked by targeted source, store and accepted snapshot tests. Any Windows CI PASS must correspond to the exact HEAD; prior green jobs refer to older code. User presets are implemented in source but physical Windows interaction still needs verification.
- Follow the current exact-SHA Windows `native` GitHub CI workflow before merge. A CI success proves compilation and automated tests, not physical sound quality.
- **Still requires owner equipment FAT**: audition all 31 faders at representative rates, verify Flat unity (0dB trims), inspect bypass/restore and `.smix`, open/close editor while live ASIO runs, verify CPU/XRUN and confirm headroom/clipping at ±12dB in a complete music mix.

No claim is made that this is a hardware-accurate proprietary dbx transfer function: frequency centres and classic panel controls are matched, while the original JUCE biquad implementation is independently authored.


## Owner-approved NEW knob (21 Nov 2026): static black-metal PNG, blue arc/dot in code

The previous V7 rollback **still owns the 31 slider visuals and their coakan**. This new explicit request adds one isolated Output Level knob, but **does not** revive rejected V8/V9 fader rendering. The existing 31 × 40 = 1240px SVG bank and 229px art height remain unchanged. Add a **100px side rail at right**; default editor is 1430 × 305px, with the dual ±12/0 scales still directly beside the faders.

- New transparent `geq31_knob_black.png` asset: 192×192, sha256 `270aaf8220aa5659a15c46531fbcab55803d6351148400df1e86f0e143f75e03`, based on the generated black-metal knob the user selected, **no embedded blue dot or ring**. Embedded in production JUCE `BinaryData`, decoded/cached once. PNG **never rotates**.
- `EqSkin::drawRotarySlider` paints the moving **blue pointer** on the brushed face and a **thin blue progress arc in the black outer recessed annulus**, both using the identical polar angle. Arc starts -135°; 0 dB is at the top (half sweep), max +6dB at +135°. Only code draws blue.
- `levelDb` parameter -24…+6 dB, double-click unity, 15ms smoothed post-31-band output multiplier inside the existing one native FX graph; no extra latency, threads or device callback. `SliderAttachment` supports native parameter update/readback. GEQ1 v4 validates and restores explicit level; v1/v2 obsolete trims remain discarded, v3 resets unity; host .armixfx preset snapshot and atomic 31+level recall/save include level, old band-only presets default unity. Factory presets restore unity; Flat button resets bands only.
- Local browser review preview is **not** the native JUCE screenshot. Actual Windows build/CTest and physical ASIO FAT remain separate. Do not publish an independent console; approved mixer `/preview/` stays unchanged.
