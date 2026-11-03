# CSS V22 — Legacy hardware-key ownership (GUI M3 reconciled)

Base: `aba0e2879fbc6cbfe42be42197aa249accb08abf` GUI M3 merged main (PR #343). V22 is based on the exact M3 GUI; it preserves meter 70px, clip latch, FX layout, WIRE, responsive header, and all existing integration code.

## Root cause
The anonymous inline stylesheet still owns V1/V18/V19/V20 shared mechanical key material, while the existing `mixer-chrome-material.css` owns the physical console chassis. Move that single contiguous shared mechanical key family into Chrome, immediately **before** the exact immutable M3 Chrome tail. Modern MUTE/SOLO/PFL owner `mixer-control-keys.css` is untouched. No WIRE/fader/FX/DOM/JS/ENGINE changes. V15/V16 WIRE, V22 selection/fader, and V30 latest button corrections intentionally remain inline for a later owner-safe audit.

## Measured results
| Measure | Before | After |
| --- | ---: | ---: |
| Late inline CSS (Unicode chars) | 34529 | 23140 |
| Chrome material CSS (Unicode chars) | 53835 | 64808 |
| `!important` in late inline | 301 | 249 |
| Relocated CSS | — | **11,389 chars** |
| Relocated `!important` | — | **52** |
| Genuinely removed CSS bytes (Unicode chars) | — | **416** |
| Genuinely removed CSS declarations / `!important` | — | **0 / 0** |
| Removed empty pseudo-element rules | — | **1** |
| New stylesheets, override layers, JS or ENGINE changes | — | **0** |

Only **416 characters** are a true CSS-text saving. The other 11,389 characters are an ownership transfer, not a bundle reduction. Removed text is a dead, declaration-free `::before{}` rule and its obsolete comment; all glints, pressed/focus/latched rules, shading and materials remain.

## Verification contract
- `tests/fixtures/css-v22-legacy-key-material-migration.json` stores full source, M3 baseline/current FNV+lengths, M3 Chrome tail fingerprint and precisely one dead rule.
- `tests/css_v22_restore.py` recreates pre-V22 M3 HTML+Chrome **byte-for-byte**. Existing V10–V21 fingerprint assertions remain unaltered; V18/V21 source tests compare the reconstructed M3 predecessor, not the later V22 live material.
- Chromium V22: 5 screen sizes × 8 folds; 3 viewport pseudo/states, hover/focus/disabled/latched, MUTE/SOLO/PFL, PAN, FX, WIRE, fader, plugin browser, 45 material tokens.
- Existing M3 UI, engine boundaries, Firefox on main and Pages publisher remain authoritative; no release claim before exact-head CI PASS.

## NEXT V23
Prioritize the remaining V15/V16 WIRE vs later-loaded `mixer-wire-dots.css` and V30 key material vs `mixer-contextual-buttons.css`. Audit live CSS winners by specificity/importance/media and dynamic WIRE/plugin/modal states, then delete only provably redundant declarations. The global `html *` legacy filter reset remains a risk requiring targeted browser evidence before removal.
