# ArSonMix CSS V27 — Scoped material guard + exact cascade-loser pruning

**Locked GUI reference:** `0215986fbbe2c67971680952f75f069950ac653e` (main #363, WIRE M2–M4, V51 portrait meter/fader, V26 style ownership; exact main CI #41743417385 and Pages #41744670156 both SUCCESS).

## Why these changes are safe

1. `mixer-chrome-material.css` used `html * ,html *::before,html *::after` to disable expensive glow/blur on **every DOM subtree**, including possible future third-party WebView/host overlays. All live ArSonMix console controls, WIRE dialogs, Plugin Browser and route choice are descendants of `.app`. Replace with `html :where(.app, .app *)` and corresponding pseudo-element selectors. Using `:where()` retains the exact CSS specificity tuples **(0,0,1) / (0,0,2) / (0,0,2)**, original `!important` and source order. No material, shading or blur is removed inside the console.
2. Five existing component CSS owners accumulated declarations overwritten **later in the same owner** by the exact same full selector, at-rule context, property and importance. All losers are deleted from original offsets; the last winning declaration and all surrounding visual values are unmodified. No selector splitting, `:is()` reweighting, or new stylesheets.
3. Global scoping changes the filter only **outside the application**. A real Chromium integration probe appended outside `.app` shows the original stylesheet removed its `blur(2px)`, while V27 leaves the outside element alone; the entire actual mixer DOM and every `::before`/`::after` filter/backdrop style remain identical.

| CSS owner | Earlier overwritten declarations physically removed |
| --- | ---: |
| Mixer Plugin Browser | 4 |
| PAN Controls | 1 |
| Fader Assembly | 2 |
| Meter Display | 11 |
| Utility Controls / FX keys | 19 |
| **Total** | **37** |

**Actual CSS characters removed:** 1340. **Existing `!important` physically deleted:** 16; new important rules: **0**. No new CSS file, JS event mutation, engine integration mutation, DOM hook edit, or geometry redesign.

## Strict evidence

- `tests/fixtures/css-v27-scoped-material-pruning.json` records every physical deleted loser with its exact substring/offset, selector, property, at-rule, priority and later winning declaration; includes immutable original/current length, 32-bit FNV and priority counters.
- `tests/css_v27_restore.py` reconstructs byte-exact six previous CSS owners and original main #363 HTML, then mounts that unmodified source for browser comparison.
- `tests/pages_css_architecture_v27_smoke.py` checks **5 viewport sizes × 8 rail-fold states**, material/pseudo/interaction/token parity at 3 sizes, and all real `.app` nodes' 3 filter properties × 3 pseudo contexts over 9 viewport-density configurations, plus the intentional non-app isolation probe.
- Historical V8–V24, V25, V26 run exclusively with their accepted pinned original assets, **not against modified V27 stylesheets**. After each historical stage the current source must be restored and `git diff --exit-code` must match exactly. M1/M3/fader/meter/FX/WIRE/browser/native bridge checks remain active on the V27 current source.
- Only changed CSS owner links are cache-busted. Do not change the published renderer architecture or release process.

## Next: CSS V28

The remaining **12,925 characters** of inline legacy V15/V16 WIRE, V19 rail and V22 layout styling still require explicit ownership. Find shadowed declarations through **whole-selector, specificity and matching media evidence** first. Review active `:is()` branches only with CSS specificity proof; never remove a dead arm blindly or move rules across earlier/later stylesheet boundaries.

**Non-negotiable:** GUI premium shading and all active MUTE/SOLO/PFL/fader/meter/WIRE states remain exactly as the approved Pages reference.
