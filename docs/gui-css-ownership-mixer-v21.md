# CSS V21 — Physical Console Chassis & Section Geometry Ownership

Baseline `main` commit: `d9e0931321ca0a4b23f8fe8a8594e49c07c79353` (V20 CI + Pages PASS).

## Root cause

The `mixer-chrome-material.css` owner already contains the shared physical
console Chrome material and V7/V8 chassis geometry. Yet the *very next*
anonymous inline block still owned the continuous **V9–V14 section geometry
and shell contract**. This split causes two editing locations for the same
physical chassis component and obscures the original cascade.

V21 closes that exact boundary: append the uninterrupted V9–V14 source block
to the immediately preceding existing Chrome owner **without reordering one
rule**. Subsequent V15 cable/socket material and V16+ hardware/button history
stay inline for independent ownership analysis. Do **not** put legacy material
inside the modern `mixer-control-keys.css`, whose own MUTE/SOLO/PFL scope
and important-count safeguards must remain intact.

## Measured source change

| Property | Before V21 | V21 |
| --- | ---: | ---: |
| Late anonymous inline CSS | 48,436 chars | **34,529 chars** |
| Physical Chrome owner | 37,973 chars | 51,880 chars |
| Exact source relocated | — | **13,907 chars** |
| Important declarations relocated | — | **130** |
| Actual CSS declarations removed | — | **0** |
| New stylesheet / override | — | **0** |

**Do not claim a 13.9 KB payload/runtime reduction.** The win is clear
source ownership and precisely one editing location for physical chassis
geometry. No new selector, token, color/gradient, CSS animation, JS handler,
DOM hook or ENGINE contract change. Version only the changed Chrome link
to prevent stale GitHub Pages assets.

## Proof and historical integrity

- `tests/fixtures/css-v21-chassis-geometry-migration.json` stores the
  complete literal moved source, exact old/new CSS and HTML FNV fingerprints,
  before/after character counts and `!important` count.
- `tests/css_v21_restore.py` reconstructs both V20 source files byte-for-byte,
  retaining all V10–V20 immutable provenance through the restore chain.
- V21 Chromium compares five viewports × eight fold combinations plus
  MUTE/SOLO/PFL, PAN, FX, WIRE, fader, meter, plugin browser, physical
  button hover/focus/disabled/latched/pressed styles, and 45 root tokens.
- The owner must be *immediately before* the transferred inline block,
  preserving original stylesheet/inline cascade sequence. No asset-relative
  `url(...)` or `@import` may be moved.

## Next V22

Analyze the now-separated V15/V16 WIRE housing and V1/V18/V19/V20/V22/V30
hardware material in the remaining 34.5k inline source. Prioritize removing
cross-owner over-specific and globally leaking styling only when equivalent
states are observed across dynamic plugin and wire usage, then consolidate
into existing component owners **without turning modern hardware-keys into a
legacy catch-all**. Coordinate with ongoing GUI M3 PR #343 / ENGINE FAT before
reconciliation; never silently overwrite those branches.
