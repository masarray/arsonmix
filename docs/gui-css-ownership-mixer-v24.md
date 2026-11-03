# CSS V24 — Fader / Meter / FX Rack: physical cascade-owner pruning

**Baseline:** `4a2005825bc57ef586aa452f1b29523a9be0295f` (PR #357 V23 merged, exact main CI and Pages PASS).

## Root cause and safe constraint

The component stylesheet was extracted in V8/V9 but successive visual corrections kept earlier declarations for the *same* full selector and at-rule context. All 26 eliminated declarations have a **later declaration in the same stylesheet, same entire selector list, same property and same `!important` priority**. The winning source positions and all other declarations are unmodified. The source ledger proves deletions rather than movements.

This specific cleanup does not solve all global specificity overlaps, and is not a reason to rewrite approved fader calibration or M3 meter geometry.

| Component owner | Removed declarations | Removed important | Removed complete rules | CSS text actually deleted |
| --- | ---: | ---: | ---: | ---: |
| mixer-fader-assembly.css | 12 | 11 | 4 | 672 |
| mixer-meter-display.css | 11 | 11 | 3 | 536 |
| mixer-fx-rack.css | 3 | 3 | 2 | 237 |
| **Total** | **26** | **25** | **9** | **1445** |

No new CSS owner, duplicate patch layer, changed JavaScript/ENGINE/DOM event hooks, geometry redesign or new visual state.

## Reversible regression and production

- `tests/fixtures/css-v24-three-component-owner-pruning.json` stores full deleted CSS slices, exact offsets, selector/at-rule/property/priority evidence and winner offsets, plus original/live FNV and lengths.
- `tests/css_v24_restore.py` recovers exact **V23 HTML + all three V23 CSS owners**, then the existing V23→V10 history is reconstructed without changing previous immutable fixture SHA/FNV.
- `tests/pages_css_architecture_v24_smoke.py` checks 5 viewport sizes × 8 folds, 3 viewport material/pseudo/interactive/token checks, fader/meter/FX states. Existing M1, M3, FX owner, WIRE, Firefox and full UI checks remain enabled.
- Fader, meter and FX stylesheet query keys increment once for correct GitHub Pages cache refresh, and M1 cache assertion is updated.
- Promotion requires exact PR HEAD Chromium CI PASS → verify no main drift → squash merge → main Chromium/Firefox UI CI PASS → Pages build+deploy same main SHA.

## NEXT V25

High-impact remaining work: the 23,140-character late inline block (V15 route choice, V16 WIRE fold and routing mechanics, V19 rail, V22/V30 keys) versus physical component owners. Move only entire proven component boundaries to existing owner CSS with **computed old/new source-order and state parity**. The `html *` material-filter reset must be assessed for selector leakage against plugin/modal states before any removal. Avoid wholesale inline-to-Chrome import or global `!important` stacking.
