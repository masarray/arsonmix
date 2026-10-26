# ArSonMix GUI / CSS Merge Reconciliation — 2026-10-26

Repository: `masarray/arsonmix`  
Audited main: `8d5f97429764272ae186a066439a26b1a79b8f39` (P0 ASIO DIRECT route fence, PR #55).  
This is an evidence-based **reconciliation snapshot**, not a new visual design or hardware audio certification.

## Merge disposition — do not replay superseded branches
- **PR #51** (`e758c806`): introduced `fx-nav` CSS ownership, replacing Button 180 specifically for seven toolbar-actions buttons and eight left-rail buttons. **MERGED**, now a historical foundational migration.
- **PR #53** (`08e09fac`): **MERGED**, owner-approved **Button 19 latching 3D navigation** now supersedes the older FX-power *appearance* from #51. Idle raised bright grey `#e0e4e8`; selected/active recessed green `#39ef72`; transient press and no fake latch for one-shot actions. Uses the existing filename `web/styles-src/21-navigation-fx-power.css` for stable build order (filename is historical, not current visual semantics).
- **PR #54** (`0747fa29`): **MERGED**, source of the latest reviewed GUI performance/layout changes: synthetic preview meters via cached DOM refs and `transform:scaleY`, pointermove listeners only during real drag, and top `.main` padding **2px**.
- **PR #55** (`8d5f974`): **MERGED**, safety guard for ASIO DIRECT route; no GUI changes. Keep native engine and DSP improvements from main.
- **PR #49** (old FX Power navigation), **PR #46** (old brushed-metal theme ARD) are **closed without merging**. The `design/brushed-metal-on-current-main-20261026` branch is **22 commits behind main and 0 ahead** at this audit. Do not rebase it over the approved current GUI as though it were newer.
- This reconciliation requires **no GUI code replay or cherry-pick**. All currently approved navigation + compositor changes are already in main.

## Current ownership contract
| Area | Present accepted implementation | Ownership |
|---|---|---|
| toolbar-actions / left rail | **Button 19 latching** (raised grey OFF, recessed green ON), 7 + 8 controls | `web/styles-src/21-navigation-fx-power.css` (historical name), `web/index.html` |
| mixer channel/master | **Button 180**, OFF `#858283/#636363/#2b2b2b`, ON contextual accents, transient 1px click | `web/styles-src/20-button-180.css` |
| actual FX Rack power/bypass | authentic `fx-power` selectors, independent of nav buttons | `web/styles-src/10-console-legacy.css` until component migration |
| responsive rail / PAN | exact PAN groove/fill/detent offsets **13/15/12**; 168px master retained | `web/styles-src/40-rail-pan.css` plus pre-existing layout |
| runtime parity | single WebView2 resource and byte-matched standalone; no runtime CSS imports | `scripts/build_ui_css.py --check` |

The earlier instructions in `AGENTS.md` saying “FX-power inspired nav look” are **history**, not a request to revert the Button 19 visuals. This block supersedes those historical directions.

## Verified CI at the main snapshot
The main commit `8d5f974` had **Chromium UI smoke SUCCESS**, **Windows Native VST3 Host SUCCESS**, and **Publish ArSonMix GUI (main dispatcher) SUCCESS**. See GitHub Actions for run logs. These do not certify real ASIO hardware performance or an FPS improvement on a particular PC.

## Remaining merge inventory / safeguards
The **12 open PRs** seen during audit (#1, #3–#10, #25, #26, #29) are pre-integration stacked/native engine work, **not currently open GUI/CSS polish PRs**. Some diverge significantly from main (e.g. #25/#26/#29 25–33 ahead and 95–96 behind; #8/#9/#10 39–46 ahead and 234 behind). Do not bulk-merge, retarget, rebase or cherry-pick them without per-PR duplicate-content and safety review; older code could erase later engine and GUI hardening.

## Follow-on CSS architecture milestones (tracked by issue #37)
1. AST-driven selector/cascade owner audit; distinguish actual collisions from media-scoped intentional reuse.
2. Consolidate breakpoint policies and remove confirmed dead selectors only after DOM / visual reachability evidence.
3. Reduce specificity and transient `!important` with component-owned, deterministic changes. Do not move `21-navigation-fx-power.css` back to FX-power optics, nor swap Button 180 onto the nav.
4. Maintain strict quality gates: `python scripts/build_ui_css.py --check`, button/PAN/baseline tests, Chromium screenshot/computed style + interactions, Windows native CI. Native ASIO hardware FAT remains a separate uncompleted release gate.

**Disposition:** GUI reconciliation is **complete on main**; the CSS modernization work in issue #37 is **still open**, and should start from the latest main rather than merging abandoned historical GUI branches.
