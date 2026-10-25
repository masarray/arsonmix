# CSS Ownership Hardening — implementation 2026-10-26
## Milestone 1 — implemented
- Five source files under `web/styles-src/` now deterministically generate **one** standalone-compatible WebView2 `web/styles.css`. Four existing embedded offline font/PNG assets remain self-contained; no runtime import/network dependency.
- `20-button-180.css` now owns the component core and final OFF/hover/ON rules in one place; duplicated obsolete earlier ON color rules were removed. Its **OFF** is flat smoked black crystal with overhead inset light and dark lower edge; **ON** has unchanged recessed shadows/colors. No geometry, DOM, JS, native DSP or PAN/fader asset changes.
- `scripts/build_ui_css.py --check` enforces source CSS and standalone HTML parity in CI; Pages compilation also refuses drift. `AGENTS.md` and the GUI lock document updated.
- Original `10-console-legacy.css` is intentionally quarantined, **not** declared cleaned. The build output changes its order only by placing the former final Button 180 OFF rules beside their owner. Validate browser-computed styling in CI before merge.
## Next meaningful milestone — responsive & legacy cleanup
- Use CSS AST + browser-computed matrix at media boundaries, 13 strips, MIX/FX Rack/Signal Flow, EASY/PRO, ON/OFF, native preview. Identify true obsolete declarations with selector reachability evidence.
- Consolidate overlapping toolbar breakpoints and per-component cascade into single owners; eliminate confirmed dead CSS, specificity escalations and needless `!important`, never mass-delete by grep.
- Integrate AST ownership diagnostics and lint budget into CI; profile UI paint before asserting performance gains.
## Validation gates
```sh
python scripts/build_ui_css.py --check
python tests/button_asset_test.py
python tests/pan_button_asset_test.py
python tests/gui_baseline_lock_test.py
python tests/ui_smoke.py
```
Windows WebView2 100/125/150% visual acceptance and real audio hardware are separate gates; no claims of those from GitHub CSS CI.
