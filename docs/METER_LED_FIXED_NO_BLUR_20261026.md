# ArSonMix — fixed LED meters / no-glow GUI (2026-10-26)

## Why
The prior meter used `transform:scaleY(--meter-scale)` on a multi-color gradient. That scaled the entire LED artwork, pulling yellow/orange/red down to incorrect positions whenever the bar was short. It was also an unnecessary source of repaint/composition pressure. The user requested a Soundcraft-style fixed-color segmented meter and **no blurred glow** on hardware controls.

Soundcraft Ui24R documents meter quantisation (LED-like segments) and optional reduced GUI meter/frame budgets; we have not copied proprietary Soundcraft JavaScript or CSS. This is an independently implemented static-band LED meter.

## Render contract
- Physical low green, mid yellow, high amber, top red bands remain stationary on the full meter track, regardless of level.
- Bottom-up visible count is determined solely by `--level` percentage and `clip-path:inset(calc(100% - var(--level)) 0 0)`. Quantized separators stay at fixed absolute positions.
- Subdued, unlit LED segments are present in the track. No smooth glow/blur on meter, CLIP latch or peak line.
- Native JUCE/WebView2 and the legacy native bridge emit the same `--level` percentage. The obsolete duplicated `--meter-scale` write has been removed; no DSP or audio callback is touched.
- Native meter interpolation is disabled for crisp LED state transitions; simulated browser meter uses existing bounded timer and peak-hold/clip logic.
- Silver/red original fader artwork, PAN PNG and geometry, relative drag, pointer capture, keyboard controls and center detent remain unchanged. Only hover/drag *visual filters* are disabled in normal GUI. Diagnostic opt-in `mixperf=baseline` still recreates historic filters for comparative A/B testing.
- Button 19 toolbar + left-rail OFF/ON still have grey/green raised/recessed state, with **hard-edged zero-blur shadows**, no hover brightening, and no text/glow blur. Buttons remain fully functional.

## Validation
Source CSS modules compiled deterministically to `web/styles.css`, inline standalone script/CSS byte-synchronised. Chromium UI regression checks CSS palette identity between levels, clip progression, visual filter policy, pressed navigation and no-jump fader. JS native bridge meter percent contract preserved. Windows Native VST3/realtime CI runs on the PR head.

The Soundcraft online demo may restrict automated retrieval; provided screenshots and publicly available Ui24R user-manual quantised-meter description are sufficient for the generic segmented LED behavior. An exact reproduction of Soundcraft's particular JS/CSS meter algorithm would require permission to examine its asset files. No proprietary source copied.

The root cause of overall Firefox pointer stutter is not yet quantitatively proven without profiler data on the affected computer. This work reduces visual filter/blur operations and corrects meter semantics, but does not claim a specific FPS improvement.
