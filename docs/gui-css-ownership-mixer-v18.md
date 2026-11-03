# V18 — Shared Operator Chrome and Legacy Section CSS Ownership

Baseline: e00ca5f66be8b40c0df75591b0aeb66d3a6c61e0 (V17 + merged GUI M2).
Only CSS, historical restoration tests and CI workflow are touched.

## Root cause and change
The 17,244-character middle HTML inline style owns hardware BTN-180 body,
pressed/disabled/focus states and console geometry, even though the existing
mixer-chrome-material.css loaded immediately after it. An extra anonymous
style owner makes changes harder to audit and invites overriding in a late rule.

V18 moves this entire CSS source **in original declaration order** to the
start of mixer-chrome-material.css, removing it from inline HTML. The second
empty style tag remains solely as a source-order compatibility position for
historical tests; it has no declarations. No new CSS stylesheet, selector,
override, generated assets, JS, DOM handlers, or ENGINE code. The existing
45 token declarations stay authoritative in the same owner. V18 Chrome link
uses css-owner-v18 to avoid stale Pages cache.

Four old important declarations (color, background, box-shadow, text-shadow)
in .console-body .switch.btn-180 were physically eliminated, because the
same selector and priority in the later original Chrome owner always win.
The rest of the material, pressed state, shadows and geometry stay intact.

## Measured source changes
- Inline CSS: 81,055 -> 63,811 characters (-17,244).
- Chrome owner: 10,984 -> 28,143 characters (+17,159).
- Combined CSS: 85 fewer characters, not 17 KB (primarily ownership).
- Actual important reduction: 4; moved important flags do not count.
- Source-order equivalence and four shadowed properties must be confirmed
  by browser regression before merging.

## Verification design
The V18 fixture tests/fixtures/css-v18-chrome-inline-consolidation.json
stores the exact prior source, removed declarations and FNV fingerprints.
tests/css_v18_restore.py reconstructs V17 HTML and Chrome CSS byte-for-byte.
Historical V10–V17 regression gates use that immutable history. Dedicated
V18 browser tests cover five viewports, eight fold combinations, actual
MUTE/SOLO/PFL, modeless Plugin Manager, PAN, fader, meter, FX, WIRE and
the 45 global material tokens. Never loosen the old fingerprint gates.

## Next V19
Audit the late-inline command-bar, contextual key and cross-component
hardware overrides. Move ownership only where cascade order can be proven,
and remove provably shadowed same-selector/same-property important
declarations. Do not change the operator-approved material.

## Parallel GUI M2 reconciliation
This V18 source was rebased onto the merged M2 GUI. M2 fader/meter owner
styles, 27x54 cap, calibrated dBFS legend, latest tests, and the three M2
HTML deltas are retained byte-for-byte. V17 restoration still normalizes
only those separately approved M2 deltas when reconstructing V16 history.
