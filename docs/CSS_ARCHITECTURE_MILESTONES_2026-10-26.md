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

## Milestone 1b: one crystal geometry for every button state
The owner rejected differences in physical elevation between OFF and ON. The shared shadow now uses the previously approved ON inset recipe on all states with fixed shadow colors; OFF is a neutral dark gray face. Hover on OFF and ON changes only brightness and never border. Tests compare computed OFF/ON and mouse-down shadows, bounds, semantic color and hover border. Generator/parity and native audio remain untouched.

## Milestone 1c — raised dark-crystal OFF, tactile ON (latest owner design)
- Same single Button 180 component owner, no extra HTML wrappers/layers or JS events. Raised OFF optical construction = solid dark slate `#202832`, 1px overhead/left specular highlights, crisp stepped 2–3px bottom bevel and restrained cast shadow.
- Recessed press/ON shadow inherits existing semantic ON surface; instant CSS `:active` depth trigger, persistent `.active/.selected/[aria-pressed]` keeps inward surface. No physical layout move or added hover border/glow.
- Filter-only hover `brightness(1.32)`; 90ms bounded press shadow tween, 150ms color, 120ms filter, reduced-motion guard; no continuous animations or will-change GPU allocation.
- Browser test asserts OFF lower bevel vs ON inset, pressed shadow and unchanged hitbox, hover brightness and border continuity. Next milestone remains responsive/legacy AST cleanup; outside this feature's scope.

## Milestone 1e — exact gray OFF tokens, SAME optics as ON (latest owner correction)
The raised cube OFF experiment was rejected. The OFF and ON Button 180 surface now references **a single retained ON inset shadow recipe**; all extra OFF raised layers and state-specific depth rules are removed. The only difference is color/edge/ink. The neutral OFF palette is `#858283 / #636363 / #2b2b2b`, ON retains its original semantic palette. Hover remains brightness-only 1.32 without border shift. The source CSS and generated standalone maintain byte parity. Tests cover exact computed OFF colors, ON/OFF shadow geometry parity, mouse-down bounds, hover and 13-strip mixer geometry. Follow-on responsive/legacy ownership refactoring is still separate.
