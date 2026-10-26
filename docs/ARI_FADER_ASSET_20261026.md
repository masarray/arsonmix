# ARI channel fader PNG (2026-10-26)

Original uploaded asset: `web/assets/faders/ARI-FADER.PNG` (36×77 RGBA, 3,801 bytes, SHA-256 `de0cd2f8250d8d4eef1ce48cb5c509f596f65134c9c618dce895eaadd54d6132`).

The black horizontal detent is at row 38. Use `translate(-50%,-38px)` for channel caps, `38px..height-39px` clamp and `trackHeight-77px` drag distance. MASTER keeps the original 31×90 red cap and old travel constant.

The CSS source owner is `web/styles-src/41-mixer-performance.css`. Embed the original PNG as a data URI for offline standalone and native WebView2. No hover/focus/drag blur, PNG filter, transition or new event listeners. Regenerate/check with `python scripts/build_ui_css.py --check`.

Regression coverage: `tests/fader_asset_test.py` (source image SHA-256 and offline parity), `tests/ui_smoke.py` (13 heads, PNG decode, geometry, keyboard range and dragging).
