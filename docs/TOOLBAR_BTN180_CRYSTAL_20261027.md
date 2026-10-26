# Toolbar BTN-180 Crystal — 2026-10-27

## Scope
Owner-directed visual redesign of the **seven toolbar-actions only**: grey OFF acrylic, luminous pearl-white selected ON, **flat rectangular crystal block** (2 px corners). This is not an image/mockup and does not reassign Button-180 class semantics. The markup/JS continue using `fx-nav`; only its toolbar selector is overridden in the existing owner `web/styles-src/21-navigation-fx-power.css`. The eight left-rail Button 19 keys, mixer Button 180 keys, FX power, native audio controls and backend remain unchanged.

## Why this shape
The original Button 180 hardware recipe contains seven inset/outline/cast shadow layers but its opaque solid body reads as rubber. Here the layer **geometry** is adapted into seven **zero-blur** optical faces: upper and left glints, darker right and bottom refractions, thin rim and crisp 1/2 px lower cast. Two linear gradients create a narrow hard top reflection and restrained planar tone. No radial/domed material, PNG, SVG, pseudo-elements, `filter:brightness`, `drop-shadow`, `backdrop-filter`, `box-shadow` blur radii, transform/layer promotion, or new JS listeners.

## State machine
- OFF: `rgba(96,111,126,.74)` smoky grey, pale legible ink.
- ON: `rgba(243,249,253,.96)` white-crystal face, deep ink, white rim; persistent only for selected radio navigation.
- Press: momentary shallower cast/deeper inset; **no DOM geometry or text scale change**. One-shot Setup/Plugins/Signal Check never latch.
- Focus: crisp white-blue outline, no fake halo.

CSS color variables `--tb180-*` are only visual skin tokens; no width, height, padding, line-height, hit target or app state touched. The material uses the existing deterministic CSS compiler and exact WebView2/offline HTML parity.

## Verification
- `python scripts/build_ui_css.py --check`
- `python tests/button_asset_test.py`
- `python tests/gui_baseline_lock_test.py`
- `python tests/ui_smoke.py` (asserts OFF/ON colors, flat no-radial reflection, no backdrop filter, seven hard-edged shadow faces, unchanged Button 19 rail, fixed press geometry, navigation state)
- `python tests/ui_firefox_perf_smoke.py`, Windows native MSVC/CTest gate.
- Review `toolbar-crystal-offon.png` artifact at desktop 1680x920 and UI layout at 1440x820. Do not interpret these as physical hardware latency/FAT measurements.
