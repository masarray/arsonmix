# V12: Eliminate shadowed CSS declarations, do not relocate overrides

Starting SHA: `f2252998b452fe3c3b312d2f0d251169b29efc12` (V11 merged). Source owner: `site/preview-v2/index.html`, last two of three inline stylesheet blocks. Existing stylesheet ownership, asset paths, JS DOM, event handlers, WIRE/ENGINE contracts, and all branded material tokens stay untouched.

## Root cause and elimination contract

The long-lived reference CSS had repeated *exact* selector/property declarations in the same conditional (`@media`) context and `!important` level, but with later, authoritative values. That leaves earlier declarations permanently shadowed and makes future edits hard. V12's audit walks the CSS syntax (respecting comments, quoted values and function parentheses), matches **identical complete selector, conditional context, property and priority**, and removes only older declarations superseded by a later value. Avoid `url()`, advanced `color-mix()`, `oklch()`, `light-dark()` and `image-set()` fallback families when values differ. Whole rules that contain exclusively shadowed declarations, without comments that would be lost, are removed. Rules with unsupported or conditional fallbacks are not blindly consolidated.

- **171** shadowed declarations removed (6 in second inline style, 165 in third), including **31** fully redundant rule blocks.
- **10,152 CSS characters eliminated** (186,176 → 176,024 total; 136,506 → 126,354 inline).
- **95 actual `!important` tokens eliminated** (1,449 → 1,354 overall; 1,194 → 1,099 inline).
- **No new CSS selector, override, token, style import, specificity escalation, or layout variable** was added.
- Unchanged owner CSS: foundation/grid/section controls/section layout/V3 layout/chrome material/artwork, MUTE key material, FX rack, PAN/fader/meter, utility controls and WIRE dots.

## Proof

`tests/fixtures/css-v12-removed-declarations.json` is a reversible ledger of all 144 source-splices (declarations or whole rules) with precise original offsets and removed text. `tests/pages_css_architecture_v12_smoke.py` rebuilds original V11 HTML from V12 source and ledger, then asserts the **exact V11 fingerprint `39885f8a`**, total length and old `!important` budget. It runs Chromium against **actual V11 restored and V12 cleaned pages**, across all eight ROUTE/FX/SEND fold combinations at 1366/1680/1920; checks 25 computed properties, 23 core controls and extra WIRE/pan/fader/toolbar samples, and actual MUTE/SOLO focus/hover states. The regular existing smoke suite continues to protect MUTE/SOLO/PFL, plugin rack and WIRE V15–43. V10 source/geometry gate now locks new expected CSS fingerprint `8cb19a32` and 176,024 total chars.

This is a **measured CSS cleanup**, not a claim that the 126 KB remaining inline CSS is all conflict-free or that all eliminated `!important` can be discarded.

**Next V13:** identify and remove shadowed declarations across component files and inline boundaries (where overlapping selectors are proven), and consolidate final hardware-key/WIRE material owners only with a detailed CSSOM winner audit and baseline/computed-state verification. Prioritize true CSS conflict reduction and renderer stability over adding another stylesheet.
