# ArSonMix mixer GUI — ten-channel ergonomic pass (2026-10-26)

## Scope and layout
- Present ten live source strips (Lead Vocal through FX Return), plus the dedicated stereo MASTER. Monitor/Aux Out strips are no longer shown in the mixer or navigation rail.
- Source width is fluid, **no 140px maximum**: ten flex-equal strips occupy the available mixer width beside a dedicated 168px stereo MASTER. Minimum 108px per source keeps the four controls usable; at narrower viewports, horizontal scrolling is deliberate instead of squeezing labels/meters.
- Source strips retain the existing six FX inserts, PAN, clip latch, fader and meters. No DSP graph, meter algorithm, VST3 engine, audio channel-routing or real-time callback code changes.
- Ø / HPF and MUTE / SOLO now share identical 9px horizontal gutters and 8px inter-button gaps. Both rows use true two-column CSS grids rather than percentage widths plus arbitrary padding. Their states, keyboard semantics and Button 180 styling are unchanged.

## MASTER finish
Use `web/assets/faders/FADER-ARI-ROSEGOLD.png` — **36×77 RGBA**, SHA-256 `730d4e6739e1eeca87c3c758777eef48dac8a29b2d2a4abc857463ee0a9d0f17` (3,782 bytes). The one-time design-time recolor uses the original silver head's geometry and highlights with a restrained rose tint, retaining deep black detent and avoiding bright yellow. No runtime filter/blur/resampling. The previous GOLD asset remains available for rollback but is not active.

## Editing
Single click on a channel footer selects the strip. Double-click opens the existing channel name/icon/color editor, with Enter/Space keyboard activation preserved. The side navigation updates with a sanitized, current name.

## Saved sessions
Ten-channel sessions are the new runtime shape. Older 12-channel saved sessions/snapshots are read compatibly by importing their first ten channels only. Reject underspecified sessions; do not resurrect removed channels. Master state, FX settings and the two removed channels' historical external data are not deleted from the user's browser storage by this migration.

## Build owner and validation
CSS `web/styles-src/42-mixer-premium.css` is appended to the existing modular ownership list in `scripts/build_ui_css.py`; generated `web/styles.css` and standalone HTML must remain byte identical. Preserve source-of-truth layout, static fader data URI, fixed-color LEDs, zero-blur optimized mode, audio event batching and legacy baseline.

Checks: `python scripts/build_ui_css.py --check`, `node --check web/app.js`, `python tests/fader_asset_test.py`, and `python tests/ui_smoke.py`. Chromium test asserts 11 total strips, right-edge alignment, evenly spaced controls, no overflow at 1680px, old fader/keyboard behavior and single-vs-double-click editor.
