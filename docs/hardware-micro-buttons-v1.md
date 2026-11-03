# Physical ArSonMix micro-buttons · V1 (approved cap family)

GUI owner: `site/preview-v2/assets/mixer-utility-controls.css`.
Runtime atlas: `micro-hardware-17x17-2x4.png` and `format-hardware-25x15-2x1.png`.

- **17×17** is the physical footprint for FX MAIN, FX slot power, phase and
  momentary remove. No outer-box resize on click.
- **25×15** is the physical footprint for M/ST; text remains real HTML, so
  changing mono/stereo does not need a new raster legend.
- **OFF column 0**: full-height silver surface with no secondary lower indicator.
  **ON column 1**: recessed semantic-color surface filling the entire frame.
  Square atlas rows are FX, phase, remove and format. Remove is momentary, never latched.
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

## Operator optical adjustment (2026-11-03)

- Lift only the PAN thumb artwork 2 CSS px from V18; leave its rail,
  input hitbox, center detent and pointer behavior untouched. The SEND thumb
  retains its original V18 alignment.
- FX remove uses muted-red `#a4494e` glyph at OFF, on the original silver
  keycap; momentary red pressed frame and white glyph are unchanged.
- Scope: PAN/utility CSS owner, revisioned preview links and browser tests.
  No change to the WIRE geometry, DSP, FX route, fader or approved M/S/P PNG.

## V20 optical-square correction

The old 17×17 atlas included a short button face plus a separate indicator below,
so its *visible face* looked compressed. V20 replaces both keycap atlases.
Each 17×17 icon key now has a genuinely square **15×15 metal face inside a
1px integral border**, without a bottom stripe, lamp, badge, drop shadow or
floating ornament. Pressing changes only the face color, not size/position.
M/ST remains 25×15 to retain its real text, also with no lower ornament.
The off Delete glyph remains muted red; the momentary red press frame remains.
Validation checks actual PNG pixels and unchanged control/WIRE behavior.

## V21 — exact supplied Delete Forever and illuminated FX power

- Replace only the FX Rack remove glyph with the **unmodified SVG path** from
  the user's supplied `delete_forever_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24.svg`.
  The source copy is `assets/delete-forever.svg`. Inline runtime SVG uses
  its identical `d` path and existing `currentColor` so OFF stays muted red
  and momentary press stays white. No substituted trash pictogram.
- Recolor **only the FX power ON** atlas frame (row 0, column 1) to a bright
  restrained green, no glow, no extra ornament. Its SVG icon stays **dark green
  `#163e2b`** rather than white, maintaining readability on the active face.
- The FX Rack power main and per-slot buttons share one existing sprite/state.
  No changes to layout, keyboard, WIRE, DSP, faders, PAN or other buttons.
- Chromium/Python regressions check exact icon path, RGBA green pixel,
  pressed contrast and stable 17×17 control geometry.

## V22 — PAN thumb optical micro-adjustment

The PAN slider thumb is raised **2 CSS px** relative to the previous V19
position (`margin-top: -12px` → `-14px` on the WebKit thumb). Only the PAN
painted thumb position changes; SEND remains `-10px`, and the 40px input
hitbox, PAN track, detent, mapping, pointer/keyboard behavior and nearby mixer
controls remain unchanged. The existing CSS V13 fingerprint and browser smoke
are updated without relaxing those regression checks.
