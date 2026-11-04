# CSS V26 — Retire unused legacy socket CSS, never touch V51 visual

**Immutable approved baseline:** `2f01022370c589205d738366242d2d2aeb77b10d` (V25 successfully merged, exact main CI #41732923188 PASS and GitHub Pages #41734263288 PASS).

## Root cause

The old V22/V30 physical button experiments targeted an HTML wrapper `.hw-switch-socket` that was removed by the modern hardware-key model. The current DOM always constructs `.mx-hw-row > button.mx-hw-key`; existing UI tests already fail if `.hw-switch-socket` materializes. This left 11 never-matchable complete CSS rules plus an empty `.gain-scale-travel` rule, including **51 dead `!important` declarations**.

V26 eliminates only whole dead rules whose *entire selector list* depends on the retired wrapper. It does **not** edit a single active styling property, stylesheet order, `:is(...)` alternative, fader/meter geometry, MUTE face or V51 PNG/color. Note: `:is()` lists containing a legacy branch are deliberately retained because even an unmatched branch can affect the whole functional pseudo-class specificity; that needs a dedicated future review with computed cascade proof.

| Existing source | Dead wrapper rules deleted | Empty rules deleted | Important declarations actually removed | Physical source bytes removed |
| --- | ---: | ---: | ---: | ---: |
| Inline V15/V22 material | 5 | 1 | 37 | 1469 |
| `mixer-control-keys.css` V30 section | 6 | 0 | 14 | 1093 |
| **Total** | **11** | **1** | **51** | **2562** |

Current inline length: 14394→12925; existing owner length: 16743→15650. These reductions are real deletions, not CSS relocation or placeholder overrides. No additional `!important`, new stylesheet, renamed selector, changed JavaScript/DOM hooks, or ENGINE code.

## Evidence and regression

- `tests/fixtures/css-v26-retired-socket-removals.json`: exact original CSS removed slices (Python codepoint offsets), old/new SHA-independent FNV/length/`!important` fingerprints, each selector and rule proof.
- `tests/css_v26_restore.py`: reconstruct exact V25 HTML + CSS owner, physically mount original styles for Chromium comparison; hard-fail on source drift.
- `tests/pages_css_architecture_v26_smoke.py`: five viewports × eight folding states; three viewports pseudo/material/interaction and design token parity; 9 live density tests to confirm modern MUTE/SOLO/PFL keys, **zero legacy sockets**, and no JS errors.
- CI continues prior V8–V24 pinned historical checks, plus V25 migration tested on **exact immutable V25 main commit**, then restores current V26 source and checks it before the current user-facing interaction suite. No rebaseline of accepted design.
- The V6 priority-budget assertion is adjusted to compare modern owner bytes against the proven original V25 tail, verifying those live controls have not been restyled, rather than incorrectly expecting obsolete V30 declarations to remain.
- Only key owner cache query changes; updated inline HTML inherently publishes with the latest page.

## NEXT CSS V27

Audit `mixer-chrome-material.css` global `html *` filter and cross-owner specificity leakage, and remaining inline WIRE/rail/toolbar rules. Treat `:is()` branches explicitly because removing even dead selector alternatives may change specificity. Incrementally rehome only proven adjacent owner boundaries; never bulk append to Chrome or rearrange WIRE links.

**Design lock:** current V51/V25 visual, shading, geometry, active/hover/pressed/disabled, meter/peak hold and routing is authoritative.
