# V15 — Cross-owner CSS cascade cleanup

Baseline: `07fd0d7958aa39c478c3870ff1b4928647dc18f0` (V14 + GUI V45).

A source-order audit identified older declarations in six existing CSS owners overwritten by later identical-selector/property/priority rules. V15 removes **95 shadowed declarations**, **15 whole redundant rules** and **37 real `!important` declarations**. Full canonical CSS shrinks **175,850 → 173,298** characters (−2,552). No new stylesheet, selector, override, or ENGINE code was created. The V45 WIRE CSS, responsive states, 3D button artwork and the JS/DOM hooks are unmodified.

Selection criteria: exact selector, media/supports context, property and `!important` priority must have a later unconditional winner at the same exact context. Conflicting modern-function fallback pairs (`var()`, `url()`, `color-mix()`, `oklch()`, `light-dark()`, `image-set()`) are retained. Byte-precise records are in `tests/fixtures/css-v15-cross-owner-removals.json`.

`tests/css_v15_restore.py` restores the six original CSS files exactly. `tests/pages_css_architecture_v15_smoke.py` tests the exact baseline `188064b0` against V15 `ad118593` using Chromium computed styles, 5 sizes (including short screens) × 8 folding states, hardware MUTE/SOLO/PFL, fader, PAN, browser/FX, and routing. Historical V12–V14 fixtures remain reproducible. Six modified owner links get the V15 query string to avoid browser cache mixing.

**Next V16:** reduce remaining legacy inline component/state CSS with dependency-aware ownership cuts, and only remove declarations after exact real-browser parity proof.
