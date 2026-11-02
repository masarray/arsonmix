# ArSonMix V10 — CSS Architecture: Console Foundation and Cascade Integrity

Base: V10 CSS source baseline `7ca51fade852dabf3ec4254fa94b7007e99768de`, **rebased on WIRE V43** `2be7c0fe15af5b3fae28e73a49e3c29ae46feb74`. No native audio, ENGINE, event-handler, DOM or routing-model changes.

## Root cause and structural repair

The reference accumulated many generations of inline rules. Moving an owner rule into an external stylesheet **after** inline CSS can revive an obsolete declaration and change the graphical result (observed in the V9 PAN collapse gate). V10 moves contiguous, clearly defined source segments to owner CSS **at the exact cascade positions where they originally occurred**.

| Owner | Responsibility | Source characters |
|---|---|---:|
| `mixer-console-foundation.css` | Root design tokens/reset, window title, command toolbar and mixer chrome | 2,653 |
| `mixer-channel-grid.css` | Console, section rail, bank and strip geometry, narrow/MASTER foundations | 2,559 |
| `mixer-hardware-artwork.css` | Original embedded silver/rose/PAN physical artwork variables | 12,460 |

The latter remains in sequence **between two retained inline style blocks**; moving it to the end would violate CSS source-order guarantees. The visual recipe, Inter font, BTN-180, MUTE/SOLO/PFL, fader, PAN, metering, WIRE V42 and plugin styles have not been redesigned.

Exactly **24 top-level CSS rules with identical selector and body** were removed as redundant, retaining the final duplicate. No different/possibly meaningful declarations were assumed obsolete.

Inline CSS characters: **187,951 → 168,504** (19,447 fewer). Total CSS after deduplication: **186,176 characters**. Inline `!important` count drops due to physically moved declarations, not stronger overrides. The combined CSS/source-order fingerprint is **`39885f8a`** (32-bit FNV1a over Unicode code points).

## Verification / release gate

- New `tests/pages_css_architecture_v10_smoke.py`: reconstructs original CSS order from the three owners and two inline blocks, checks the exact fingerprint/length, WIRE V42 ownership, Pages asset manifest, and 1366/1680/1920 Chromium geometry/Inter/fader hardware.
- Repairs V8 and V9 ownership tests to inspect **both** inline blocks. Previously, testing only the first block could falsely pass after extraction.
- Pages builder and deploy workflow now guarantee byte-identical copying for all V10 CSS assets. Existing **WIRE V43 dot/socket CSS** and its /preview/ alias copy are preserved from V43; V10 does not duplicate V43's earlier fix.
- Existing eight-state collapse, FX/Plugin Manager, WIRE V15/V40/V42 and native GUI workflow tests remain mandatory. PR cannot merge on a failed visual or interaction gate.

## Next milestone

V11: reconcile the late-era shared section geometry and toolbar/navigation active/inactive state styles. Reduce duplicated conflicting `!important` declarations by measured computed-style comparison before/after; preserve exact material and pointer hit-testing. The full 168 KB inline source is **not yet clean**; V10 is a controlled foundation milestone rather than a claim of complete refactoring.
