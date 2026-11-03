# V17 — Plugin Manager and Fast FX Picker CSS ownership

Baseline: `37dd696976da8062616ea24d15f2b5aca135db03`. Scope is **CSS only**; no ENGINE, DOM, JS or fader/meter changes.

**Cause:** Plugin Manager, scanner and quick FX selector styles were physically split across early inline HTML blocks even though `mixer-plugin-browser.css` existed.

**Change:** Move 4136 exact CSS characters (1733 manager and 2403 scanner/fast picker) into the **existing** plugin owner and delete the original inline rules. No new stylesheet, selector, override, dynamic state, shadow or material is added. Version the owner as `css-plugin-owner-v17` for Pages cache consistency.

**Evidence:** `tests/fixtures/css-v17-plugin-overlay-migration.json` and `tests/css_v17_restore.py` reproduce V16 source byte-for-byte with original owner and FNV fingerprints. `tests/pages_css_architecture_v17_smoke.py` verifies 4 resolutions, 8 collapse states, modeless browser open/minimize/restore/close, real Fast FX, More toolbar, and 3 comprehensive state sweeps. Historical V10–V16 tests retain the exact previous source for fingerprint validation.

**Metric:** −4136 inline characters; +4285 owner characters. These declarations were relocated, **not eliminated**. No total CSS reduction or `!important` reduction is claimed.

**Next V18:** shared BTN-180 / toolbar CSS material ownership and property-level pruning after cascade and visual parity evidence.
