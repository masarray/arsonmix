# Issue #56 — Phase 2: single-canvas preview meter A/B and Firefox coverage

**Status:** experimental rendering candidate; default `optimized` remains DOM-based until the affected Firefox/Windows machine proves an improvement. This does not touch native ASIO/WASAPI/DSP, JUCE metering or the real-time audio callback.

## Engineering hypothesis
The browser GUI still mutates 14 individual meter DOM bars around 25 Hz. On some Firefox GPU/driver paths the combination of multiple CSS filter layers and continuous repaint/compositing may contend with pointer feedback. The earlier lighter-hover + selective layer work (#58) did not yet establish causality. Replacing these 14 animated DOM bars with one transparent preview-only Canvas2D drawing surface is a bounded experiment, **not** a performance claim.

## Implementation
- `?mixperf=canvas` explicitly enables `paintPreviewMeterCanvas()`: one transparent canvas overlay within the workbench, `pointer-events:none`. The 12 channel and stereo master bars use cached meter rectangles measured once per redraw/scroll/resize. Bar gradients/stripes are rendered once per unique geometry and reused with `drawImage`. Peak-hold tick positions are drawn into the same canvas. The original 14 `[data-meter]` and peak elements stay in the DOM (only hidden visually in canvas mode), preserving geometry and clip-latch buttons.
- JS meter values and 1600ms peak-hold logic are shared with the DOM path. Canvas mode skips per-meter `style.setProperty('--meter-scale')` and per-peak `style.bottom` writes, but retains `clipLatch` semantics and the master simulated LUFS-I/dBTP labels.
- Cache invalidation happens on `renderMixer`, `renderMaster`, workbench resize and mixer horizontal scroll, not on every frame. Canvas is hidden outside MIX and is created **only** when the explicit URL flag is set and there is no native JUCE backend.
- No change to approved silver/red metallic fader art, Button 19, Button 180, rack power, PAN center detent, layouts, channel/master DSP or native meter bridge. The original DOM meter is still the default and fallback if canvas initialization fails.
- `site/preview/perf.html` gains **Canvas** as a seventh comparison mode. Use the existing recorder to compare `optimized` (DOM) vs `canvas` on **identical Firefox hardware**.

## Verification
- `python scripts/build_ui_css.py --check` and existing Chromium screenshot/geometry/drag/preset tests.
- Chromium Canvas functional smoke verifies 14 meter mappings, a painted canvas, unchanged CLIP targets, suppressed DOM meter style writes, and correct view switching.
- Dedicated Playwright Firefox smoke verifies actual Firefox Canvas2D rendering and switching, without hard-coded FPS gains. CI is Linux/headless: it **cannot** certify the Windows Firefox/GPU improvement.

## Next decision
Collect at least 3 paired 10-second recordings for `optimized` and `canvas` (same device, similar pointer movement, record full JSON with p95 and over33Ms). Check for perceptual smoothness alongside Firefox Profiler paint / compositor trace. If Canvas consistently lowers p95/dropped frames without visual mismatch, promote to browser-preview default in a separate reviewed PR. If Canvas performs worse, keep DOM as default and investigate WebRender software fallback or image/filter cache behavior. Issue #56 remains open until evidence arrives.
