# ArSonMix — Restorable GUI checkpoint · 2026-11-03

**Status:** integrated *reference GUI* acceptance baseline, not a finished native Windows mixer, not physical hardware FAT.

- **Code snapshot before this documentation-only lock:** `main@3cdcb98438a2f4f71da00e721ca7dd4f3a4bfacc` (PR #336 merged).
- **Pre-M1 CSS cache fix recovery branch:** `checkpoint/gui-m1-wire-v48-buttons-v23-precache-20261103` at `2f9be6657aa646cebd502d2d7343c3ad6c13ae1a`. Do **not** use the pre-cache copy as a newer visual revision.
- **Preferred final recovery branch:** `checkpoint/gui-m1-wire-v48-buttons-v23-m1cache-20261103` (created from the exact post-reconciliation `main` commit after this lock is merged and checked). Its Git ref, not a mutable notion of "latest", is the recovery point.
- **Published page:** https://masarray.github.io/arsonmix/preview/ ; source `site/preview-v2/index.html`; aliases include `/preview-v2/` and `/preview-buttons/` built using `scripts/build_pages.py`.
- **CI evidence before cache-fix merge:** main UI smoke [#41597552546](https://github.com/masarray/arsonmix/actions/runs/41597552546) PASS; Pages [#41597927095](https://github.com/masarray/arsonmix/actions/runs/41597927095) PASS.
- **Cache-fix head evidence:** PR #336 UI smoke [#41597737625](https://github.com/masarray/arsonmix/actions/runs/41597737625) PASS. Post-merge `main` run [#41599630359](https://github.com/masarray/arsonmix/actions/runs/41599630359) is the exact revision's required independent release gate.

## Reconciled source truth — already merged

| Area | Accepted version | Canonical owner and acceptance condition |
| --- | --- | --- |
| Operator reference GUI | PR #278 direction, current Pages mirror on `main` | Single compact live-console **reference**; preview is simulated and is not a native PCM renderer |
| M1 calibration | PR #334, commit `82ae2677`; cache PR #336, commit `3cdcb984` | `mixer-fader-assembly.css` and `mixer-meter-display.css`; share calibrated ruler/meters, preserve exact gain and dBFS mapping. Both stylesheet query keys must be `css-owner-v9-ruler-m1` |
| PAN | PR #335 / V23, commit `2f9be665` | `mixer-pan-controls.css`; WebKit PAN thumb `margin-top:-15px!important`, SEND thumb **unchanged** `-10px`. Preserve PAN 40px input hitbox, slider rail, detent and pointer/keyboard behavior |
| FX utility keys | PR #330 V20 + PR #332 V21 | `mixer-utility-controls.css`; physical 17×17 square-faced cap PNG, **no lower bar/ornament/glow**; main and slot FX ON gently bright green with dark `#163e2b` power glyph, OFF non-white |
| FX delete | PR #332 V21 | Use the **exact user-supplied** `delete_forever` path, stored in `assets/delete-forever.svg`. OFF muted red `#a4494e`; white glyph only during momentary pressed state. Never substitute another trash path |
| MUTE/SOLO/PFL | Reviewed native-resolution assets on current Pages | Preserve the already accepted PNG imagery/state/geometry and `mixer-control-keys`/hardware owner; not a reason to replace other buttons with that family |
| WIRE | V16 CSS ownership, V47 + V48 on `main` (V48 commit `ab8ae09`) | `mixer-wire-dots.css` and `mixer-wire-out-align-v48.css`; preserve OUT dot/caption as one aligned physical control, live IN/OUT anchors, snap, selected incoming cables, source/destination colors, bank selection and dense compact caption behavior |
| FX Rack layout and state | V8 owner + V21 material | `mixer-fx-rack.css` owns rack layout; existing add/remove, bypass, reorder/power interactions and scroll behavior must not be rewritten by unrelated cosmetic work |
| CSS architecture | V9–V16 owner and reversal ledgers | Existing owner boundaries; do not add later conflicting cascade patches, excessive `!important`, duplicate component layers or global icon states |

**Contract:** the visible button **surface** must have the desired aspect ratio, not merely the PNG plus shadow bounding rectangle. Preserve labels, hit targets, icon geometry, state affordances, dense readable typography and light GPU cost. Do not guess or redraw the supplied Delete SVG or approved assets.

## Change / merge safety gates

1. **Start from the final checkpoint's exact Git SHA** and compare against current `main` before editing. Keep the checkpoint branch untouched; make new work branches or PRs from current `main`.
2. Declare a narrow **component owner** and list intended visual deltas. Do not overwrite a whole `index.html`, entire CSS, or generated `web/styles.css` with stale design-branch copies. Native `web/**` uses `web/styles-src/**` + deterministic `scripts/build_ui_css.py` and must be reconciled separately.
3. Keep `site/preview-v2/index.html`, linked assets, public `/preview/` and `/preview-buttons/` synchronized through `scripts/build_pages.py`; bump only impacted CSS/asset URL cache identifiers when content changes. Never mark a preview "live" before the exact-head Pages deploy reports success.
4. Run and retain the existing Chromium acceptance: `tests/pages_m1_meter_gain_calibration_smoke.py`, `tests/pages_micro_fader_visual_v18_smoke.py`, `tests/pages_micro_button_assets_v1_smoke.py`, `tests/pages_png_buttons_wire_reconciliation_smoke.py`, `tests/pages_wire_v46_precision_smoke.py`, and V13/V16 CSS ownership regressions. The canonical GitHub UI-smoke workflow covers these; compare on real rendered browser states, not only source selector strings.
5. Require **PR head CI success**, then **merged `main` CI success**, then **Pages deployment success**, with the same exact SHA recorded. Avoid merging while another GUI owner changes the shared file; reconcile any races first.
6. **No blanket merge** of stale reference branches, old layouts or draft native engine/FAT into this visual checkpoint. Reapply and inspect individual accepted changes only. Never force-push `main` or the checkpoint refs for routine styling work.
7. If a future change regresses the visual design: inspect the checkpoint branch and the diff from its SHA, recover only the affected files/assets in a dedicated PR, and rerun tests. **Do not hard-reset a moving `main`** because that could destroy good engine or unrelated progress.

## Open work streams intentionally *not* promoted

- `#271` and `#313`: native/WebView GUI adapter and approved M/S/P native assets; a separate integration lane with its own generated bundle, not an excuse to replace the Pages snapshot.
- `#266`, `#270`, `#310`, `#315`, `#321`: engine/production/native FAT in draft or independent baselines; no claim of real audio engine parity or validated hardware until one exact-SHA integrated Windows build and physical FAT passes.
- `#285`, `#292`, `#302`, older concept/design branches: references or superseded partial work, not safe mass-merge candidates.
- Existing documented issues such as real audio crackle and incomplete universal ChannelId/native VST3 routing remain **outside** this GUI-only checkpoint.

**Acceptance meaning:** a stable, reversible, reviewed visual/interaction reference with verified CI and publish, **not** a finished audio product. Future native implementation must follow the active PRD/ARD/puzzle ABI in `AGENTS.md`.
