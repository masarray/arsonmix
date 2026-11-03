# CSS V23 — Chrome + WIRE Provable Cascade Winner Cleanup

**Source baseline:** `9b286f486bc59cd0e7a663912787b00b73a30a18` (V22 merged, post-merge Chromium/Firefox PASS, Pages deployed). Scope exclusively existing Pages physical Chrome and WIRE CSS owners. No JS, ENGINE, modern MUTE/SOLO/PFL, asset/color or geometry change.

## Root cause

Historically iterative physical hardware revisions left redundant declarations in their *actual existing component owner* even after the V20/V22 inline cleanups. We identify a declaration as a loser only if **the normalized full selector list, enclosing @media/@supports context, CSS property and !important priority are identical to a later declaration in the same stylesheet.** Those earlier declarations cannot win in a supporting browser. We do not remove a selector for absence from the initial DOM, simplify dissimilar specificity, or treat relocation as savings.

## Measured actual deletion

| Owner | CSS chars deleted | Declarations deleted | !important deleted | Entire rules removed |
| --- | ---: | ---: | ---: | ---: |
| Physical Chrome material | 452 | 10 | 8 | 5 |
| WIRE patch material | 458 | 15 | 12 | 1 |
| **Total** | **910** | **25** | **20** | **6** |

All savings are actual **CSS text deletion**. No relocation, added CSS file, override/patch layer, selector rewrite or fallback-invalid value change. Important tokens such as `--v13-route`/`--v13-send` remain defined on `.console-body`; newer declarations remain unchanged and in their original positions.

## Evidence and history

- `tests/fixtures/css-v23-cascade-winners.json` stores each exact deleted source slice and original offset, plus 25 winning declaration witnesses, scopes/priorities, and immutable old/new fingerprints.
- `tests/css_v23_restore.py` reconstructs original V22 HTML, Chrome and WIRE **byte-for-byte**.
- Old V10–V22 historical restore chain now consults this *new inverse stage first*; original historical ledgers and FNV contracts are never edited.
- V23 Chromium: five viewport sizes × eight fold permutations; three viewports of state/pseudo/material/45 tokens and MUTE/SOLO/PFL/PAN/FX/WIRE/fader/plugin interaction. Existing WIRE V40–V48, calibrated fader/meter, GUI M3 and Pages CI remain required.
- Cache-bust only modified Chrome and WIRE asset URLs, so browsers fetch revised CSS after Pages publication.

## Next V24

The remaining 23,140-character inline block still owns V15 route-choice overlay, V16 conditional fader/fold layout, V19 rail folding and V22/V30 operator key material. Their *later-loaded* WIRE/contextual and fader owners make naïve movement hazardous. V24 should use computed cascade state evidence before extracting those contiguous component rule groups into the correct existing owners, and audit whether globally-scoped `html *` anti-filter styling leaks into plugin/modals. Avoid moving every inline rule into Chrome merely to reach zero inline characters.
