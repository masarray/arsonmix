# CSS ownership milestone V7 — Inter and small-context mixer controls

Updated: 2026-11-03. Builds on V6 [mixer-control-keys.css](/masarray/arsonmix/blob/main/site/preview-v2/assets/mixer-control-keys.css) without altering V6 MUTE/SOLO/PFL.

## Actual control-size audit

| GUI control | Existing surface | V7 decision | Active indication |
|---|---|---|---|
| Channel MUTE/SOLO/PFL | 29px, 3 equal columns | Keep V6 single SVG cutout and static edge shine | Red / amber / cyan |
| FX rack enable | 17px × 17px | **Icon-only micro-key** with offline Material vector | Teal/green |
| Per-slot FX bypass/enable | 16px × 17px | **Icon-only micro-key**, maintain slot action & index | Teal/green |
| Reverse polarity | 17px × 15px | **Compact Ø/⊘ legend**, not a 29px keycap squashed | Warm amber |
| Processing width M/ST | Min 25px × 15px | **Short text micro-key**, never a tiny multi-layer SVG cutout | Cyan for stereo |
| Remove FX | 13px × 16px | **Momentary icon-only action**, never latched | Soft neutral / red while actively pressed |
| Add/open FX | Remaining rack row | Preserve plain action / picker | No LED |
| WIRE sockets/routing | V36 custom vector/ports | Do not modify | Existing routing context |
| General toolbar navigation | Different navigation semantics | Keep current system for future independent audit | No false hardware lamp |

The previous implementation used overlapping `.mini`, `.fxslot-row .power`, `.fx-remove`, `.format-badge`, and `.mini.phase.on` styles while the DOM had to preserve these old classes. The V7 controls now use `button.mx-util-key[data-util-kind]`; 20 retired compact-control style declarations were removed from the active inline preview CSS, not merely overridden.

## Inter embedding policy

- Single variable Inter WOFF2 face **reused byte-for-byte** from `web/styles-src/00-font-and-assets.css`, the font already embedded in native WebView production.
- Pages font stylesheet: `site/preview-v2/assets/inter-embedded.css`.
- Apply `"ArSonMix Inter"` to all UI DOM and pseudo text and actual SVG knockout text. SVG icon graphics remain paths with no font dependency.
- No external font/CDN or downloaded runtime asset. Embedded `data:font/woff2;base64` supports offline standalone previews and Pages.
- `scripts/build_pages.py` copies Inter + utility control assets **byte-identically** beside the user-facing `/preview/mixer.html` and `/preview-v2/index.html`. Avoid a second unsynchronized skin.

## Performance & architecture guardrails

- Utility CSS is scoped to `.console-body .strip button.mx-util-key` and semantic `data-util-kind`; does **not** style toolbar, plugin picker, fader or WIRE.
- No SVG filter, blur, canvas animation, hover transitions, or unnecessary dynamic observers.
- Utility button state comes from the existing `data-action` callback and `aria-pressed`; native/source audio routing is untouched.
- The global `font-family` rule intentionally overrides historical Segoe UI / Arial / Consolas / Georgia in GUI CSS. Font weight/size/layout are not rewritten.
- No JavaScript is necessary to decorate 15px control buttons. Shared SVG path icons are rendered by existing `materialIcon`.

## Acceptance

`python tests/pages_inter_utility_v7_smoke.py` checks the **actual** Pages preview at 1366 and 1680, verifies offline font loading and computed families, utility dimensions, semantic LEDs, click/toggle restoration, single original action attributes, and V6 controls preserved.

Run existing regression suite, especially `tests/pages_controls_v6_smoke.py`, `tests/preview_v15_drag_wire_smoke.py`, `tests/preview_v2_v7_visual_smoke.py` and full `UI smoke (Chromium)`. Publish only after both CI and Pages deployment pass.

## Remaining CSS work

Continue to extract stable owners by area rather than adding more global `!important`: FX rack layout and picker, PAN/fader/meter, routing/WIRE, toolbar, dialogs. Keep the existing WIRE V36 and engine contracts under regression before any of those extractions.
