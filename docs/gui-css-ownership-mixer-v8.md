# CSS Ownership V8 — FX Rack and Plugin Manager

Date: 2026-11-03. Previous milestones: V6 hardware M/S/PFL; V7 embedded Inter + utility buttons.

## Source audit

The actual user-facing GitHub Pages preview (`site/preview-v2/index.html`, mirrored to `/preview/mixer.html`) accumulated **234,538 inline CSS characters** and **1,951 `!important` declarations** before this milestone. FX rack and Plugin Manager selectors had been progressively overridden by older V7–V35 layers.

Rather than add a terminal stylesheet of stronger selectors, V8 **physically extracts** each exclusively owned rule from the inline CSS, keeping the rule's declarations and its relative order inside the new owner. Shared layout selectors (route/send/FX, strip/rail height, WIRE geometry) remain where they already live because they have multiple owners.

| CSS owner | Ownership | Rules relocated |
|---|---|---:|
| `site/preview-v2/assets/mixer-fx-rack.css` | FX section background/height, rack rows, FX picker, scroll area, collapse/density FX-specific variants | 61 |
| `site/preview-v2/assets/mixer-plugin-browser.css` | Modeless Plugin Manager surface, search header, list, window sizing, minimized/hidden states | 26 |
| `site/preview-v2/assets/mixer-utility-controls.css` (V7) | FX power, per-slot power, phase, M/ST and remove actions | unchanged |
| `site/preview-v2/assets/mixer-control-keys.css` (V6) | MUTE/SOLO/PFL 29px hardware backlights | unchanged |
| `site/preview-v2/assets/inter-embedded.css` (V7) | Embedded variable Inter across GUI and SVG text | unchanged |

Rebased on WIRE V40 (preserved untouched). This removal reduces inline CSS to **225,481 characters**, and its `!important` declarations to **1,833**. The original declarations are in the corresponding CSS owner files, not globally eliminated. These figures refer to the current reference Pages HTML only, not the independent native WebView source.

## Ownership boundary

1. FX rack CSS cannot change WIRE/send, bus, general fader, PAN or layout geometry (rail `--fx-h` is owned by the layout subsystem).
2. Plugin Browser CSS cannot govern unrelated channel identity, toolbar, more tools, or quick effect picker. Shared modal/popup rules stay in console shell for now.
3. Existing `data-action`, `fxOn`, `slotPower`, `openPlugin`, drag-reorder, filtering, minimization and keyboard operations remain unchanged. This PR is CSS/build/test-only.
4. The two public URLs `/preview/` and `/preview-v2/` receive identical CSS assets through the Pages builder; no separate preview-only fork.
5. No additional hover paint, blur, moving glow, new `!important`, JavaScript watchers or CPU/GPU cost was introduced.

**Review note:** Moving a prior inline rule into a later-linked external stylesheet can change cascade priority against the few shared rules that legitimately remain inline. This is why all multi-viewport geometry, collapsed states, and plugin browser tests are required. This milestone does not claim pixel-identical output without checking CI screenshots.

## Verification and regression

- `python tests/pages_css_ownership_v8_smoke.py`: asset loading, CSS ownership budget, FX rack geometry and folding, modeless plugin manager open/minimize/restore/close, V6/V7 survival, 1366 and 1680 views.
- `python tests/preview_v2_v7_visual_smoke.py`: 1366/1666/1920 strip/rail/FX geometry, complete collapse permutations and fader invariants.
- `python tests/preview_v15_drag_wire_smoke.py`: real pointer OUT→IN WIRE and bus routing, no engine behavioral changes.
- `python tests/pages_inter_utility_v7_smoke.py` and `python tests/pages_controls_v6_smoke.py`: typography, utility, MUTE/SOLO/PFL.
- `scripts/build_pages.py` and `.github/workflows/pages-publish.yml`: both CSS owners copied byte-identically into the public preview alias.

## Next ownership stage (separate PR)

PAN controls and fader/meter layout must be addressed in **distinct owners**, because they contain real pointer hit testing and dense legacy `!important` constraints. Capture the rail/fader and live-meter geometry baselines before moving code. No bulk rewrite that weakens WIRE regression tests.

The public reference explicitly remains **simulated meters / no native audio**; this CSS refactor is not a claim of native stability.
