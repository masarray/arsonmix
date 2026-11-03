# V16 — WIRE component source ownership

Baseline `1ee7223576baaaa19fa0f5b25363c817f4f08edc` (approved V47 WIRE and V15 CSS).

**Root cause**: WIRE layout, ports, summary, patch workspace and physical cable rules occupied more than 39 KB of late inline HTML CSS despite the existing `mixer-wire-dots.css` component owner. This made the latest WIRE owner an additional cascade layer rather than a single, traceable responsibility. V16 moves only **223 top-level WIRE-specific rules unchanged** into the beginning of that existing owner, before later authoritative V47 physical dot/caption declarations.

**Measured change**: late inline style block 102,100 → 62,794 characters (−39,306); all three inline blocks 124,497 → 85,191; initial linked owner+inline CSS 173,298 → 133,992 characters. V16 changes *location* of 336 `!important` rules (1,317 → 981 in the early CSS aggregate), not their total count in the combined page. The existing `mixer-wire-dots.css` increases 9,600 → 49,178 bytes. No new stylesheet, class, override, font, interaction contract, or visual theme. `@media` WIRE declarations are left in place at the original media context rather than stripped and made unconditional.

The `tests/fixtures/css-v16-wire-inline-migration.json` ledger records exact offsets and raw source for every migrated rule, plus the owner CSS prefix to revert byte-for-byte. `tests/css_v16_restore.py` reconstructs original HTML and original WIRE stylesheet for browser-baseline tests. Historical V11–V15 source ownership and visual comparisons continue reconstructing the prior source. New `tests/pages_css_architecture_v16_smoke.py` verifies exact original/current CSS fingerprints, 5 screen sizes × 8 fold states, and real MUTE/SOLO/PFL, fader, PAN, Plugin Browser and WIRE controls at 3 full resolutions.

The wire stylesheet query version is `v47-css-v16` for cache parity on Pages/WebView2. Keep subsequent V47+ WIRE adjustments authored in their own CSS owner; do not reintroduce inline rules.

**Next V17**: migrate and consolidate the remaining inline shared hardware key and tool material into existing Chrome/control-key ownership after inspecting all state selector and cascade dependencies. Preserve the physical key cap shading, native control dimensions and dynamic WIRE geometry.
