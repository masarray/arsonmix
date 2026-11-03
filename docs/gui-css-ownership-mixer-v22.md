# ArSonMix CSS V22 — Legacy Mechanical Key Material Ownership

Base: `5efa4f08ca609acf134c23c2ccc9aa5d6e01796e` (V21 merged, main UI smoke and Pages publication successful).
Scope: the **V1 / V18 / V19 / V20 historical shared mechanical key material** formerly in the anonymous late inline block. This is an ownership refactor, **not** a mixer redesign or V22 visual redesign.

## Root cause and ownership decision

The existing `mixer-chrome-material.css` holds physical console chassis and material through V21, but V1/V18/V19/V20 shared button mechanics, sockets, semantic hardware tokens, anti-glow/anti-filter properties, and state CSS remained in the late inline block. Those generic legacy rules cover toolbar, route-choice, legacy strip switch and other console keys: putting them in modern `mixer-control-keys.css` would violate the MUTE/SOLO/PFL single-owner contract.

Transfer the unchanged V1–V20 legacy shared material to the **existing immediately preceding Chrome owner**. Keep later V19 folding, V22 selection/fader, V25/V27/V28 constraints, V30 final button corrections and V15/V16 WIRE in inline until their separate cascade/owner audit. No other owner, JS, DOM/ENGINE contract or control asset changes. Version only the modified Chrome query string.

The one removed source slice was a pseudo-element `::before{}` with **no declarations** (plus its accompanying obsolete explanatory comment). It cannot contribute any rendering properties. The remaining glint properties and all relevant active/hover/focus/disabled states are preserved.

## Proven metrics

| Measure | Pre V22 | V22 | Meaning |
| --- | ---: | ---: | --- |
| Late inline CSS | 34529 chars | 23140 chars | **11389 relocated**, not deleted |
| Chrome CSS | 51880 chars | 62853 chars | +10973 chars |
| `!important` in late inline | 301 | 249 | **52 moved**, none deleted |
| Genuinely removed CSS text | — | 416 chars | Dead comment + empty pseudo rule |
| Removed CSS declarations | — | **0** | Do not report as declaration reduction |
| Removed whole rule with declarations | — | **0** | One empty pseudo-element rule removed |
| New stylesheets/patch layers | — | **0** | Same existing Chrome owner |

**Total CSS source decreases only 416 characters**, not 11389. The first 11389 characters only change ownership. The global `html *` legacy filter reset remains a known leakage candidate and is **not** removed without proving every dynamic context and browser. V30 remains an authoritative late material generation, so V22 does not claim final single-rule ownership across all versions.

## Evidence and historical protection

- `tests/fixtures/css-v22-legacy-key-material-migration.json` records the literal moved source, one empty rule deletion, source fingerprints, exact lengths and priorities.
- `tests/css_v22_restore.py` reconstructs **V21 HTML and Chrome CSS byte-for-byte** and verifies both original and current fingerprints. `css_v21_restore` calls this reverse stage; V10–V21 immutable checks are retained, not rebaselined.
- `tests/pages_css_architecture_v22_smoke.py` exercises five viewports × eight fold masks; three viewports of pseudo-element/hover/focus/disabled/latched material parity, interactive MUTE/SOLO/PFL/PAN/FX/WIRE/fader/plugin comparisons and all 45 root tokens.
- Existing Chromium, Firefox, Page publishing and native boundary gate remain unchanged except for the added V22 verification step and one screenshot artifact.

## NEXT V23 — WIRE + final key material isolation

1. Audit V15/V16 inline route-choice/WIRE and V30 final physical key material **against their later linked owners** using complete cascade, specificity, importance, at-rule context, and dynamic states. A source-preserving transfer to a later-loaded owner is *not* intrinsically cascade-preserving.
2. Remove genuinely shadowed declarations only when the winning rule is proven in all applicable media and live states, not merely absent from initial DOM.
3. Then split selection/fader/section-fold ownership by the existing component boundaries; leave `mixer-control-keys.css` isolated.
