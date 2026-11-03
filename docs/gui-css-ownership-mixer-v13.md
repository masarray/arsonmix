# V13 — CSS owner conflict consolidation

Base: `dddd4d83d5996b83378582275cb55e6edc258517` (merged V12). Scope is strictly **CSS source in owned components**. The canonical HTML, JS event handlers, WIRE V43 material, MUTE/SOLO/PFL dedicated key-face owner and ENGINE are untouched.

## Audit — the root cause

A cross-stylesheet map found 54 exact selector/property/importance collisions within earlier console owners and the inline history (including 20 chrome-token overrides), plus repeated selector declarations within later independent component owners. V13 intentionally resolves the latter first without moving root material tokens or changing an earlier CSS source-order assumption. Retiring material chrome tokens without moving authoritative values to a valid owner would make the ownership worse, not cleaner.

V13 removes earlier values **only** when a later declaration with the **same selector, media context, property and importance level** wins in the same owner file. Different-value advanced CSS fallback families (`url`, `var`, `color-mix`, `oklch`, `light-dark`, `image-set`) are retained for caution. Whole empty rules are removed only where comments are not lost. There are no new overrides.

| Component CSS owner | Shadowed declarations removed | Whole redundant rules removed | Old → new CSS chars | `!important` removed |
|---|---:|---:|---:|---:|
| FX rack | 10 | 7 | 6,103 → 5,567 | 6 |
| Plugin browser | 15 | 0 | 3,668 → 3,253 | 15 |
| PAN | 2 | 0 | 4,996 → 4,953 | 2 |
| Fader mechanical assembly | 67 | 8 | 20,508 → 17,646 | 67 |
| Meter/VU | 21 | 4 | 10,436 → 9,005 | 21 |
| **Total** | **115** | **19** | **−5,287** | **−111** |

Control key CSS, utility controls, WIRE V43 and every earlier console owner are byte-identical to V12. The 7-owner + 3-inline V12 fingerprint remains `8cb19a32`.

## Verification

`tests/fixtures/css-v13-owner-removals.json` records each original source span and offset. `tests/pages_css_architecture_v13_smoke.py` restores **original owner files to temporary co-located asset names** and loads a baseline HTML that refers to those files, preserving CSS-relative asset resolution. A real Chromium browser compares that original with current CSS at 1366×768, 1680×900, 1920×1080, all 8 ROUTE/FX/SEND folding permutations, and actual MUTE, SOLO, PFL, plugin manager open/minimize/restore/close, hover fader and PAN change. Computed geometry, material/shadows and interaction states are compared, with screenshots attached in Actions.

Original V8/V9/V10/V11/V12 gates and full V15–V43 WIRE, FX/GUI–engine workflow smoke remain mandatory.

## Next V14: authoritative shared token ownership

Remove the **remaining early-owner vs later-inline** conflicts. Centralize the final tokens in a single authoritative owner, instead of deleting their earlier owner and letting last-minute inline overrides become the design system. Perform an explicit CSSOM winner inventory and token-consumer analysis before rewriting. Continue to preserve original hardware button shading, signal routing and motorless fader behavior.
