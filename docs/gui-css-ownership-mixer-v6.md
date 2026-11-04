# ArSonMix GUI CSS ownership — mixer controls V6

## Problem verified (2026-11-02)

The publicly served `/preview/` is generated from `site/preview-v2/index.html`, not from the native GUI workstream. The previous preview accumulated **~252,248 inline CSS characters**, **2,250 `!important` declarations**, **105 occurrences of `.switch-section`** and **206 occurrences of `.switch`**. These are source-text counts before this refactor, not unique computed styles.

The inherited MUTE/SOLO markup used `.button.switch` inside `.codepen-buttons-wrapper`, while PFL used a separate `.hw-switch-socket`. V30/V33/CodePen and later V5 selectors kept fighting to own pressed material, lamp, lip, size and position. Chromium screenshots at 1632×958 showed low-contrast OFF lettering, artificially outlined controls and inconsistent PFL geometry. More last-in-file overrides would worsen maintainability.

## Ownership decision

| Concern | Single owner after V6 | Deferred / explicitly not owned |
| --- | --- | --- |
| MUTE/SOLO/PFL DOM | `strip(t)` emits **3 direct sibling buttons** `.mx-hw-row > button.mx-hw-key` | No CodePen wrapper or PFL-only socket |
| Hardware key material | `site/preview-v2/assets/mixer-control-keys.css` | Legacy inline rules for unrelated mixer controls |
| Key SVG / text cutout | `site/preview-v2/assets/mixer-control-keys.js` | No duplicate SVG text layers |
| ON/OFF authority | Existing `data-action`, `t.mute/t.solo/t.cue`, `aria-pressed` | The key renderer does not dispatch controls |
| Public `/preview/` artifact | `scripts/build_pages.py` copies these same 2 assets beside `mixer.html` | No independent Pages key style fork |
| WIRE, FX graph, faders, meters | Original modules preserved | Not modified by this pass |

**No old `.switch` class may be added to `.mx-hw-key`.** Old switch-targeted selectors are now dead for the three migrated buttons, ending their cascade conflict without touching the unrelated workflow. The obsolete CodePen CSS block and the later broad V5 injected CSS block have been deleted from the active preview HTML.

## Small-key specification

- The three controls are **equal height 29 px**; 3-column grid with minimum usable width 26 px at 1366–1680 viewport and no overlap.
- The OFF state is a brushed graphite cap with a **pale ice-white ambient lamp bed behind one actual SVG letter cutout**. OFF text must remain readable before the channel is selected or powered.
- The ON state is red (MUTE), amber (SOLO), or cyan (PFL). LED color is never assigned from track color. Use a single mask text and no overlay duplicates.
- Exactly one white top highlight is brightest at the center and transparent at both ends.
- A tiny, asymmetric gradient only near the bottom gap indicates backlight leakage; it never becomes a fully lit neon border.
- No hover visual effect, animation loops, SVG blurs, canvas frames or GPU filters.
- Click/keyboard interactions and WIRE patching are unchanged. Disabled and focus states remain accessible.
- Avoid converting tiny 17 px FX icons into unreadable four-letter backlit keys. FX and phase utilities remain scoped to their own legacy component in this milestone.

## Progressive CSS refactor sequence

1. **V6 — Core M/S/PFL:** replace their DOM and delete overlapping CodePen/V5 styles; isolate the CSS/JS into two small owned files, verify them in real Chromium and both Pages routes. **Implemented on this branch.**
2. **Next — FX/utility toggles:** inventory actual device sizes, distinguish icons from tactile letter keys, migrate only controls that can sustain a legible cutout. Create separate `mixer-processing-controls.css` owner and remove its superseded inline rules in the same commit.
3. **Next — strip layout:** migrate legacy `.switch-section` placement and other strip geometry to a `mixer-strip-layout.css` owner. Only then remove the old `.switch-section` class and its layout selectors, while running 1366, 1680 and 1920 visual geometry tests.
4. **Next — general stylesheet:** extract CSS by stable component ownership (navigation, routing/WIRE, rack, fader, meter, dialog) without changing design. Track cascade collisions and `!important` counts; do not bulk-rewrite multi-era rules or risk the working WIRE state.
5. **Build gate:** fail if retired CodePen wrappers, PFL-only hardware socket or inline V5 CSS return; verify one SVG mask text, semantics, click states, positive widths, no overlay, no hover repaint, and byte-identical Pages asset publication.

## Acceptance and rollback

- `python tests/pages_controls_v6_smoke.py` (new owner / visual checks);
- `python tests/preview_v15_drag_wire_smoke.py` (existing OUT→IN and BUS logic fully retained);
- `python scripts/build_pages.py --out /tmp/arsonmix-pages-v6 --sha local --branch test`;
- existing `UI smoke (Chromium)` and `Publish ArSonMix GUI` workflows, including exact asset copies.

Deploy only after passing checks. Merge to `main` changes `/preview/` and `/preview-v2/` but not native audio. Rollback is a revert of the V6 CSS-owner PR; do **not** resurrect a third conflicting CSS overlay.
