# CSS V28 — Eliminate 13 KB late inline physical-console CSS with exact cascade parity

**Accepted GUI checkpoint:** `12442945526342b5116d7d42be5a591af63492ed`, main GUI **V59**, selected-only WIRE cable visibility and v8 native reconciliation. Prior user-visible GUI UI CI #41815151720 SUCCESS, GitHub Pages #41816184232 SUCCESS.

## Root cause and ownership

`site/preview-v2/index.html` still contained **three embedded `<style>` nodes**, two empty and one late **13,043-character physical console cascade** (V15 routing choice and WIRE sockets; V16 folded channels; V19 rail; V22 physical key/selection; V25 icons; V27 toolbar responsive; current GUI V28 fader-height token). It sat **immediately after** the already-existing `mixer-chrome-material.css` stylesheet link and **before** the modern hardware keys owner. This linked Chrome owner already governs console chassis, hardware key material, WIRE body proportions, rail, command bar, and route choice surface, making it the correct existing physical-console home. There is no added CSS sheet or cascade patch.

**Migration:** concatenate `old chrome CSS + exact inline CSS` into the existing Chrome file (NO rearrangement or edits); remove the three now-empty/relocated style elements; cache-bust only the Chrome link. There are no `url()`, imports, `@layer` or fonts in the moved CSS, no JS CSSOM reads, and no relative asset resolution difference. The declaration stream at the join is byte-for-byte the same. The external owner remains immediately before the former inline style and modern buttons owner.

| Before | V28 |
| --- | --- |
| Inline CSS 13,043 characters + 2 empty style nodes | Inline style elements: **0** |
| Existing Chrome CSS 63855 characters | 76898 characters |
| Combined CSS chars: 76898 | **exactly unchanged** |
| Moved `!important` declarations: 118 | 118 (same values/order) |
| Declarations added or removed | **0** |
| Stylesheets created / ENGINE / JavaScript changes | **0** |

This is a **real HTML payload and CSS ownership cleanup**, not CSS byte savings; CSS bytes move into an existing cacheable external sheet. Preserving premium shadows, gradients, restrained LEDs, fader well, selected-only WIRE rendering, modal layers and all responsive states is more important than artificially reducing the CSS count.

## Immutable validation

- `tests/fixtures/css-v28-inline-chrome-owner.json`: exact moved CSS source, original/current file lengths, priority budgets, FNV-32 fingerprints, original cache query and exact baseline commit.
- `tests/css_v28_restore.py`: **byte-exact restoration** of approved GUI V59 HTML and previous Chrome CSS; hard fail on any unapproved drift or reordering.
- `tests/pages_css_architecture_v28_smoke.py`: source-order equality, 5 viewport × 8 folded rail states, 3 viewport material/interaction/pseudo/token comparisons, and a **full console DOM comparison** of 48 computed properties and critical pseudo-element properties across 3 viewports × 3 density modes. No JS errors tolerated.
- CI retains immutable V8–V27 historical gates (each pinned to its exact approved source) followed by the current V59 V28 gate, then the entire modern V59/WIRE/FX controls and Firefox suite. No obsolete baseline is rewritten.
- No publish/pipeline revisions, external asset copies, or audio-engine edits.

## NEXT CSS V29

1. Inspect the **newly centralized** physical console cascade for empty `@media` blocks, whole-owner dead selectors, and priority collisions with section/route/wire owner sheets. Eliminate only same-selector, same `@media`, same-importance true losers; show original/current browser states before deleting any priority rule.
2. Move only **provably source-order-independent** WIRE/rail/toolbar material declarations to their dedicated existing component owners. Never blanket-move to earlier or later stylesheets: this risks cascade and new GUI V59 wire geometry.
3. Reassess `:is()` specificity without breaking hardware buttons or WIRE states.

**Visual design lock:** the existing V59 ArSonMix GUI/preview is authoritative. No beautification/re-coloring or inactive visual state regression is permitted.
