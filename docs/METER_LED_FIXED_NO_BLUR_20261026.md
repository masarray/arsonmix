# ArSonMix — fixed LED meters / no-glow GUI (2026-10-26)

## Why
The prior meter used `transform:scaleY(--meter-scale)` on a multi-color gradient. That scaled the entire LED artwork, pulling yellow/orange/red down to incorrect positions whenever the bar was short. It was also an unnecessary source of repaint/composition pressure. The user requested a Soundcraft-style fixed-color segmented meter and **no blurred glow** on hardware controls.

Soundcraft Ui24R documents meter quantisation (LED-like segments) and optional reduced GUI meter/frame budgets; we have not copied proprietary Soundcraft JavaScript or CSS. This is an independently implemented static-band LED meter.

## Render contract
- Physical low green, mid yellow, high amber, top red bands remain stationary on the full meter track, regardless of level.
- Shared preview/native painter retains raw `--level` and quantises `--led-lit` to whole 7px cells, so the palette never scales and LED cells never partially illuminate. CSS has a `--level` fallback.
- Subdued, unlit LED segments are present in the track. No smooth glow/blur on meter, CLIP latch or peak line.
- Native JUCE/WebView2 and the legacy native bridge emit the same `--level` percentage. The obsolete duplicated `--meter-scale` write has been removed; no DSP or audio callback is touched.
- Native meter interpolation is disabled; the preview retains bounded scheduling, peak hold and CLIP latch. A WeakMap caches meter sizes, with one ResizeObserver on the stable mixer root; no layout read per native telemetry tick.
- Silver/red original fader artwork, PAN PNG and geometry, relative drag, pointer capture, keyboard controls and center detent remain unchanged. Only hover/drag *visual filters* are disabled in normal GUI. Diagnostic opt-in `mixperf=baseline` still recreates historic filters for comparative A/B testing.
- Button 19 retains its mechanical grey/green state with zero-blur shadows and full function. Selected seams are sharp and native modal backdrop/scan bar have no blur glow.

## Validation
Source CSS modules compiled deterministically to `web/styles.css`, inline standalone script/CSS byte-synchronised. Chromium covers 10/30/60/85/98% whole-cell thresholds, palette stability and peak/CLIP. Chromium UI regression checks CSS palette identity between levels, clip progression, visual filter policy, pressed navigation and no-jump fader. JS native bridge meter percent contract preserved. Windows Native VST3/realtime CI runs on the PR head.

The Soundcraft online demo may restrict automated retrieval; provided screenshots and publicly available Ui24R user-manual quantised-meter description are sufficient for the generic segmented LED behavior. An exact reproduction of Soundcraft's particular JS/CSS meter algorithm would require permission to examine its asset files. No proprietary source copied.

The root cause of overall Firefox pointer stutter is not yet quantitatively proven without profiler data on the affected computer. This work reduces visual filter/blur operations and corrects meter semantics, but does not claim a specific FPS improvement.
