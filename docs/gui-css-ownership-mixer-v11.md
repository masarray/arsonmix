# ArSonMix V11 — Shared Section Layout & Chrome Ownership

Source: `main` after merged V10, commit `6d71b1c09d567b6cb087aed75d91135b323b1696`. V11 changes **reference HTML CSS only**, with Pages/CI manifests; **no ENGINE or JS/DOM changes**.

## Real ownership change (not another override layer)

Four complete **contiguous** source sections were extracted from the two surviving inline CSS blocks, with stylesheet links inserted at their original cascade positions. All original rules/declarations are byte-for-byte preserved. No new `!important`, base selector, style override, state selector or artwork are added.

| New owner | Responsibility | Rules | Source characters | Existing `!important` relocated |
|---|---|---:|---:|---:|
| `mixer-section-controls.css` | Route/SEND/EQ/TRIM/footer/control primitive surfaces | 81 | 6,888 | 3 |
| `mixer-section-layout.css` | Rail row alignment, section geometry, 3 density presets, fold height/state precedence | 110 | 10,667 | 0 |
| `mixer-v3-layout-contract.css` | Legacy V3 mechanical physical layout and MASTER geometry contract | 44 | 4,008 | 62 |
| `mixer-chrome-material.css` | V6 central metal, surface colors, shared chrome/header/rail material tokens | 52 | 10,435 | 190 |

**287 CSS rules physically removed from inline** into four owned sources. Inline reference bytes: **168,504 → 136,506** (−31,998; −19.0%). **255 previously existing `!important` are relocated**; absolute combined total stays **1,449**. Combined CSS is still **186,176 chars**, with the **same exact FNV fingerprint `39885f8a`**: moving them neither introduces new overrides nor changes source order. The 24 identical duplicate rules removed in V10 remain removed.

V11 is not a claim that the remaining 136 KB CSS is conflict-free. There are intentionally untouched legacy WIRE, advanced hardware key, FX, identity and V34 workspace selectors that must not be blindly removed based on name similarity.

## Regression proof

- V10 source audit upgraded from hardcoded two-block assumptions to **all seven owner CSS files and three inline blocks in actual DOM sequence**. It reconstructs CSS exactly and asserts the original fingerprint, total CSS bytes, 136 KB inline budget and 1,449 combined / 1,194 inline `!important`.
- New V11 Chromium gate reconstructs a separate baseline HTML using the owner CSS as inline blocks, then compares **geometry plus 25 computed style properties**, including `background-image`, `box-shadow`, `font-family`, material and border, on 23 selectors. Checks every ROUTE/FX/SEND fold combination (8), at 1366×768, 1680×900, 1920×1080; toggles real MUTE and compares clicked state.
- Existing visual, PAN/fader, metal MUTE/SOLO/PFL, plugin, WIRE V15/40/42/43, and native workflow tests remain mandatory. Pages generator and CI copy all CSS owners verbatim to `/preview/`.

**NEXT V12:** isolate remaining late-era WIRE/route-workspace and hardware-key override families after computed-style winner audits; reduce *conflicting* `!important` (not just move them) only when visual/interaction evidence supports deletion. Consider central audit tooling for CSS selector specificity, shadowed declarations and dead styles with a bounded, machine-checkable baseline.
