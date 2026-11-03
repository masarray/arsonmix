# CSS Ownership V9 — PAN, Fader, VU Meter

Based on main V8 (WIRE V40). This is a CSS-only visual reference migration, **not a native engine or audible-PCM change**.

- PAN: 35 rules → `mixer-pan-controls.css`
- Physical fader assembly: 116 rules → `mixer-fader-assembly.css`
- VU/peak meters: 49 rules → `mixer-meter-display.css`

**200 CSS rules physically removed from inline source.** On WIRE V42 main: inline characters 223629 → 188002; `!important` occurrences 1808 → 1454 (354 moved to owner CSS, none added). Rule declaration ordering within each owner is preserved.

Shared strip/rail dimensions, collapse combinations, WIRE connector geometry and selectors combining PAN/fader with other components intentionally remain at layout ownership. Moving those blindly breaks alignment. Existing embedded Inter, V6 hardware MUTE/SOLO/PFL, V7 utility keys and V8 FX/plugin owners are unchanged.

Pages builder/deployer copies all three assets byte-identically to the public /preview/ alias and /preview-v2/. The V9 Chromium gate checks loaded styles, 1366×768 / 1680×900 / 1920×1080 PAN/rail alignment, fader hitbox and 23×48 cap, meter visibility, PAN keyboard, and JavaScript exceptions. Existing eight-way collapse, WIRE pointer, FX Browser and key regression gates continue to run.

Reference preview meters are simulated. Passing these visual tests never proves native Windows audio quality or production PCM.

## CI correction — PAN panel material ownership

The first Chromium run exposed a real ROUTE-collapsed surface regression: an old PAN-specific gradient was overriding the newer flat V13 strip-lower material. Removed five obsolete PAN **panel-background** declarations from the extracted owner; the shared strip surface now remains the sole source of PAN face material. PAN slider/thumb gradients remain untouched (these belong to the PAN input itself). The regression is covered by the existing 8-state physical collapse Chromium gate and a V9 static owner assertion.
