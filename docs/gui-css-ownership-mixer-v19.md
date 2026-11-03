# CSS V19 — Chrome/Toolbar Ownership and Dead Cascade Pruning

Baseline `4ed608890c662240a112f89b3d6d0d9e31bcc7d2` (V18 merged, CI & Pages PASS).

## Audit and owner boundary
- The first anonymous toolbar block (1,017 characters) appears immediately
  after `mixer-section-controls.css`. Append it to this existing owner.
- Early V7/V8 shared operator Chrome + physical section/fader layout rules
  (9,830 characters) appear immediately after `mixer-chrome-material.css`.
  Append this **contiguous original source prefix** to that existing owner.
- `mixer-control-keys.css` is the sole modern .mx-hw-key MUTE/SOLO/PFL
  owner and MUST remain unchanged. Its hardware-owner CI gate caps important
  declarations and excludes retired .hw-switch-socket keys. V30 legacy
  button material is **NOT** assigned to that owner.
- Six important properties in late inline CSS lose to identical selector,
  media and priority in later fader/WIRE owners. Ten empty rules do nothing.

## Exact cascade and deletion
- Migrate both source chunks byte-for-byte across immediately adjacent
  link/style boundaries: external Chrome link before late inline, and
  section-controls link before first inline. No new stylesheet or layer.
- Remove only six proven shadowed important properties: global V15 closed
  SEND height, fader travel top/bottom, and selected-strip border width,
  opacity and box-shadow. Keep the V15 short-height rule.
- Remove ten empty legacy blocks. Do not alter MUTE/SOLO/PFL material, SVG,
  fader/meter owner, plugin handlers, WIRE routing or native ENGINE contract.
- Version two modified owner links to prevent stale Pages CSS cache.

## Source accounting
- Inline CSS: **63,811 → 52,297** characters (−11,514).
- **10,847 relocated** to existing owners; **667 actually removed**.
- **6 !important** properties and **10 empty rules** truly eliminated.
- No bundle-size improvement claimed for relocated CSS; source-order parity
  verified by computed-style and full event handling tests.

## Reversible verification
`tests/fixtures/css-v19-operator-owner-migration.json` captures exact source
and before/after lengths and FNV hashes for HTML, section controls and Chrome.
`tests/css_v19_restore.py` recreates the complete V18 baseline byte-for-byte.
V10–V18 historical tests retain original source fingerprints without weakening
safety gates. V19 Chromium validation covers five viewports × eight folded
section combinations and three material/interaction sweeps across MUTE/SOLO/PFL,
PAN, Plugin Manager, FX, fader/meter, WIRE and 45 canonical root tokens.

## Next V20
Prune old anonymous V1/V19/V20 keycap generations after *property winner*
and rendered state analysis; preserve current V30 hardware detail. Then migrate
other component-scoped FX, WIRE and layout rules to their owners without
creating new cross-component important cascades.
