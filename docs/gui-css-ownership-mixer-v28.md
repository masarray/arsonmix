# CSS V28 — Remove 13,043 inline CSS characters without modifying the approved /preview/ GUI

**Real authoritative baseline:** `main@46f600b143c8ef000a717ccb79a9c841a2589766`, exact previously accepted [single /preview/](https://masarray.github.io/arsonmix/preview/) GUI. **/preview-v2/ is an internal source, not an independent public preview.** `scripts/build_pages.py` deterministically assembles /preview/ from this internal HTML plus accepted PNG button materials. This refactor preserves that pipeline and its authority.

## Root cause and exact owner migration

The internal HTML still held three inline `<style>` nodes: two empty and one **13,043-character late physical console cascade**. The last nonempty inline style was immediately after the existing Chrome material CSS link. Migrate the complete CSS string unchanged to the **END** of the immediately preceding existing `assets/mixer-chrome-material.css` and remove only the three HTML style elements. Therefore **old Chrome CSS + old inline CSS = new Chrome CSS** byte-for-byte, including every declaration and its original order. No new stylesheet or selector. No new priority or removal of any premium highlight/gradient.

Chrome owns rail, route-choice, WIRE physical surfaces, toolbar and the late material/glow corrections. Later component CSS owners retain their exact relative cascade order. The only changed link is Chrome's cache version, ensuring both published WebView build and native-asset resolver receive the new owner source.

The move itself **is not a stylesheet byte saving** (CSS bytes are moved from HTML to an existing owner); it removes 13,043 CSS characters and two empty style tags from the HTML, giving one less in-document styling surface and clear inspectable ownership.

## Explicit guardrails

- Preserve exact actual approved /preview/ assembly, PNG MUTE/SOLO/PFL keys, V59 V8 WIRE behavior and GUI/Engine integration contracts; do not publish /preview-v2/ as a separate UI.
- `tests/fixtures/css-v28-inline-chrome-owner.json` records exact source CSS/HTML with fingerprints and unchanged 118 `!important` migrated declarations.
- `tests/css_v28_restore.py` reconstructs **byte-exact 46f600b143c8ef000a717ccb79a9c841a2589766** internal source HTML and Chrome CSS; its cheap source lock runs on **every** GUI CI.
- `tests/pages_css_architecture_v28_smoke.py` compares reconstructed pre-V28 vs new actual DOM on five responsive viewports × eight rail-folds; three responsive viewport pseudo-state, interactive, material and token checks; all real `.app` elements with 48 computed style properties over nine viewport/density modes.
- Run expensive V28 Chromium comparison on `css/v28-*` PR, manually, or release-integration PR. Other iterative PRs keep fast default CI.
- The existing `tests/gui_baseline_lock_test.py` contract and all current `AGENTS.md`, `scripts/build_pages.py`, Pages redirect and single-preview checks are **untouched**. In particular, no incorrect assertion that /preview-v2/ is the approved public GUI.
- Older historical V8–V27 migration fixtures remain immutable, with their same pinned source checks/restore.

## NEXT CSS V29

Remove only verifiably overwritten physical material declarations and CSS cascade conflicts from the existing Chrome owner, with full same-selector/priority/media context analysis and all responsive computed-state parity. Avoid moving live rules between nonadjacent owner sheets. Strengthen generated `/preview/` visual parity as native stitch progresses.

**Approved /preview/ premium layout is binding; no visual redesign, no extra alternate GUI, no JS/ENGINE modification.**
