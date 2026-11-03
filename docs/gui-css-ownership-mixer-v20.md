# CSS V20 — Eliminate Earlier Shadowed Material Generations

Baseline: `80a155c3da4a436c59cb7819892b5843358f3f73` (V19 main, full CI and Pages PASS).

## Architectural root cause
The V19 late anonymous inline stylesheet retained V9/V11/V12/V13 layout overrides,
V1 mechanical-button material, and newer V19/V20/V22/V30 material generations.
Within that single author stylesheet, some declarations were unconditionally
overwritten by later declarations with **the exact same normalized selector,
CSS property, media/supports scope and `!important` priority**. These earlier
declarations could never win; accumulating them adds style-resolution and
maintenance overhead and makes visual ownership harder to reason about.

V20 removes only these demonstrable earlier losers. It does **not** attempt to
simplify different-specificity selectors, split shared button groups, or remove
rule families merely because a current DOM query finds zero matches. Such
removals could break dynamically mounted legacy/plugin state and are deferred.

## Measured implementation
- **53 declarations** removed, including **51 `!important`**.
- **7 entire earlier selector rules** become removable; 28 individual
  declarations are removed from rules retaining at least one live property.
- CSS text genuinely shrinks **3,861 characters**. This is actual CSS deletion,
  **not** a relocation falsely counted as a bundle-size reduction.
- Late inline CSS: **52,297 → 48,436 characters** (−7.38%).
- Its `!important` count: **482 → 431**.
- No new CSS sheet, selector, layer, token, workaround or override.
- No changes to native ENGINE, JavaScript state/hooks, MUTE/SOLO/PFL modern
  component owner, WIRE, fader/meter owner, or current visual design system.

## Safe audit method
The deterministic V20 author-source audit matches exact full selector lists
(after whitespace normalization), same CSS `@media`/`@supports` context, same
property and priority, and confirms a strictly later declaration exists
within the same inline style owner. Each removal and its surviving winner
are recorded in `tests/fixtures/css-v20-legacy-shadow-pruning.json`.
Only complete rules with **all** declarations shadowed are removed entirely;
other rules retain their live properties, focus/disabled states, and pseudo
elements. This is not a blind search/replace or generalized CSS minification.

`tests/css_v20_restore.py` reconstructs original V19 HTML byte-for-byte
with strict lengths+FNV checks; the V10–V19 historical tests retain their
immutable source fixtures via the restoration chain. V20 additionally compares
real Chromium states (rest, hover, focus, disabled, latched), material pseudo
elements, five responsive viewport/fold variants, and full existing control
interactions with the exact V19 page. No weakened visual/test assertions.

## NEXT V21
Scope the remaining ~48 KB of inline styling into the existing layout, FX,
WIRE and hardware owners after computing true winning declarations across
different specificity and dynamically mounted states. Prioritize redundant
component-level `!important` rules, then extract contiguous ownership
boundaries only when source order is proven. Integrate with M3 GUI branch only
after independent CSS V20 verification and conflict-free reconciliation.
