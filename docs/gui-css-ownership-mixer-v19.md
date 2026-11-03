# CSS V19 — Final Operator Key Material Ownership and Dead CSS Cleanup

Baseline main: `4ed608890c662240a112f89b3d6d0d9e31bcc7d2` (V18 PASS and Pages deployed).

## Root cause
- The complete V30 operator keycap material, pressed/latched/hover/focus detail
  (8,746 characters), still lived in anonymous late inline CSS immediately
  before the already existing `mixer-control-keys.css`.
- Initial 1,017-character command-bar/More-toolbar rules lived immediately
  after the existing `mixer-section-controls.css`.
- The late inline block carried six exactly shadowed `!important` properties
  with the same selectors, media contexts and priority in later owners
  (fader assembly and WIRE), plus ten empty rules.

## Architectural migration, no patch stacking
- **Append** the exact 1,017-character first inline block to the preceding
  `mixer-section-controls.css` owner, keeping the original order.
- **Prepend** the exact 8,746-character V30 key material to immediately
  following `mixer-control-keys.css`, keeping the original order and keeping
  subsequent current v6 hardware-key CSS authoritative.
- Delete six shadowed properties (global V15 closed SEND height; fader travel
  top/bottom; selection outline opacity/border-width/box-shadow). Do not delete
  the V15 reduced-height media state or the working selection rim.
- Delete ten genuinely empty legacy CSS blocks. Do not alter declarations in
  the 45 global Chrome material tokens.
- Version both owner links for Pages caches. No new stylesheet, additional CSS
  selector, specificity, animation, JS, DOM or ENGINE code.

## Measured
- Total inline: **63,811 → 53,381** characters; reduction 10,430.
- Physically relocated: **9,763** characters; genuinely deleted: **667**.
- `!important` reduced **6** declarations and empty rules reduced **10**.
- Owner CSS size grows due to relocation, not claimed as an equal bundle saving.
- Shared button color, border, pressed/latched relief, shadows, disabled/focus,
  and MUTE/SOLO/PFL geometry must remain unchanged.

## Verification
`tests/fixtures/css-v19-operator-owner-migration.json` plus
`tests/css_v19_restore.py` restore exact V18 HTML, section owner and key
owner with strict lengths+FNV hashes. Previous V10–V18 source gates remain
strict with previous owners restored for immutable baseline comparisons.
V19 verifies CSS material and active control styles with Chromium at five
viewports × eight collapse masks, plus full MUTE/SOLO/PFL, PAN, plugins,
fader/meter, WIRE and 45 material tokens at three desktop sizes.

## Next V20
Study the remaining ~53K late inline source by **real selector winner/cascade
coverage** before pruning older V1/V19/V20 push-button recipes. Move only
confirmed component-scoped rules to existing hardware, layout, FX and WIRE
owners. Prioritize actual reductions of conflicting rules over source moves.
