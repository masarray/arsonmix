# Physical ArSonMix micro-buttons · V1 (approved cap family)

GUI owner: `site/preview-v2/assets/mixer-utility-controls.css`.
Runtime atlas: `micro-hardware-17x17-2x4.png` and `format-hardware-25x15-2x1.png`.

- **17×17** is the physical footprint for FX MAIN, FX slot power, phase and
  momentary remove. No outer-box resize on click.
- **25×15** is the physical footprint for M/ST; text remains real HTML, so
  changing mono/stereo does not need a new raster legend.
- **OFF column 0**: raised silver surface with the semantic indicator strip.
  **ON column 1**: recessed colored face. Square atlas rows are FX, phase,
  remove, format. Remove uses ON on pointer-down only; do not latch.
- Button labels, accessible names, original SVG icons and click handlers remain
  untouched. No image object, repaint loop, JS icon-generation or duplicate UI
  state. RGBA backgrounds always transparent and never scaled per state.
- The existing V6 `mixer-contextual-buttons.css` owns standard, wide and
  WIRE-workspace *button materials*, NOT micro-switches, MUTE/SOLO/PFL, nor
  physical WIRE IN/OUT sockets. Use CSS for width-variable labels, not stretched
  56×37 PNG sprites.
- WIRE geometry, cable endpoint anchoring, data-action handlers, faders, PAN
  and native audio callback are unrelated and must not be edited to adjust buttons.

Validation: `python tests/pages_micro_button_assets_v1_smoke.py`
and the full Chromium/Firefox Pages regressions.
