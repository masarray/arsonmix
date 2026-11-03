# V14 material-token single ownership (ArSonMix)

Base SHA: `e42dc3c584b3f6dab70062c746b117b2b6569c0a`. Reconcile after parallel GUI edits before merging.

**Root cause**: 8 metal/theme variables in `mixer-chrome-material.css` were redefined by later inline `:root`; 3 BTN-180 values existed in an **earlier** inline palette but were already overridden by the **later** Chrome owner. The Chrome-approved `var(--am-crystal)`, `#899aa0`, and `#12212a` remain authoritative. Additional hardware-key and 30px-key material variables were dispersed across four late inline `:root` groups, with two empty `:root` rules. The final browser cascade uses only the last per-token definition; modifying the owner stylesheet previously appeared to do nothing for 11 tokens.

**Correction**: Make the existing `mixer-chrome-material.css` (not a newly appended stylesheet) the sole owner of all **45 unique shared material tokens**, preserving every final winning value. Remove all eight late inline `:root` rules, merge the 11 shadowed global definitions, preserve all component-specific CSS, state selectors, shadows, gradients, MUTE/SOLO/PFL, WIRE V44, and ENGINE hooks. Bump only chrome URL cache version to v14.

Source audit: `56` root declarations → `45` authoritative unique definitions; original combined CSS `176024` chars → `175850` chars; inline `126354` → `124497`; `!important` remains `1354` (this milestone removes ownership conflicts, *not* urgency tokens).

**Proof**: `tests/fixtures/css-v14-material-token-ownership.json` contains exact removed inline roots and the original Chrome root block so the prior V13 document can be reconstructed byte-for-byte. The existing V12 baseline fingerprint is asserted against this reconstructed pre-V14 source. V14 Chromium regression compares original V13 rendering with V14 across 3 viewports × 8 fold states and actual MUTE/SOLO/PFL, PAN, fader, FX/Plugin Browser and WIRE states. Capture all 45 computed custom properties from `:root` and verify every original/after value. UI/ENGINE/WIRE V15–V44 tests remain mandatory.

**Next V15**: dependency-aware cleanup of obsolete color/material declarations in non-root selectors across earlier owners and inline CSS, addressing specificity and `!important` only after CSSOM winner evidence, with a new visual equivalence baseline.
