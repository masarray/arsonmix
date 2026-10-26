# ArSonMix Theme Engine — START HERE (Next Thread / Architect Handoff)

**Updated:** 2026-10-26 | **Architecture status:** PROPOSED | **Implementation status:** NOT STARTED
**Epic:** [#44](https://github.com/masarray/arsonmix/issues/44) | **Draft architecture PR:** [#46](https://github.com/masarray/arsonmix/pull/46)

## First instruction to incoming engineer

**Do not re-design or rewrite the approved Brushed Metal CSS. The exact 14KB reference implementation is already committed in this branch.** Use it as the appearance specification while migrating its visual values to token-owned component renderers. Production GUI layouts remain exclusively controlled by the existing core CSS/HTML/JS.

Read this order, no shortcuts:
1. `AGENTS.md` and `docs/GUI_BASELINE_LOCK_2026-10-25.md` (binding GUI preservation contract).
2. [Brushed Metal Reference](../design-references/brushed-metal-v1.0/README.md), [exact adapter.css](../design-references/brushed-metal-v1.0/adapter.css), [source fingerprint](../design-references/brushed-metal-v1.0/reference.json).
3. [ARD Multi-Theme Engine](ARD_MULTITHEME_ENGINE_V1_2026-10-26.md), [implementation ladder](THEME_ENGINE_IMPLEMENTATION_PLAN_2026-10-26.md).
4. `scripts/build_ui_css.py`, the modular `web/styles-src` files, `web/index.html`, `web/app.js`, `web/bridge.js`, `tests/ui_smoke.py`.
5. Re-fetch newest `main` and compare with reference SHA `3478823`; do not reset or bulk overwrite later audio/GUI commits.

## What already exists versus what does not

| Artifact | State |
| --- | --- |
| Approved brushed-metal **visual CSS** | **COMMITTED verbatim**: `design-references/brushed-metal-v1.0/adapter.css` |
| Manifest/checksum from the original CSS and source HTML | **COMMITTED**: `reference.json` |
| Build-on-demand preview without changing the repo's GUI source | **COMMITTED**: `scripts/render_brushed_reference.py` |
| Immutable CSS+HTML parity and checksum test | **COMMITTED**: `tests/theme_reference_fixture_test.py` |
| Theme engine ARD, roadmap, role/property ownership | **COMMITTED** |
| Actual `studio-default`/ `brushed-metal` runtime token registry/compiler | **NOT YET IMPLEMENTED** |
| Theme chooser and persistent user preference | **NOT YET IMPLEMENTED** |
| WebView2 theme persistence and native acceptance | **NOT YET VERIFIED** |

The new developer should be able to build the full historical skin **without writing CSS**. See the reference README for copy/paste shell commands. No full 600KB HTML is stored as a duplicated theme—the unchanged canonical standalone at `3478823` plus this exact 14KB overlay reconstructs it.

## Critical architecture

```text
                 GUI SOURCE — SINGLE OWNER OF LAYOUT
       channels / Master / rows / FX slots / fader travel
                           |
                Semantic Component Renderer
      button · rail · chassis · FX glass · PAN · LCD · modal
                           |
               Visual tokens (NOT geometry)
                    /              \
          studio-default       brushed-metal
           (original)         (approved silver)
                    \              /
          validated ThemeManager (root data attribute)
                           |
          isolated UI preference; never audio DSP
```

**Keep these hard boundaries:** theme cannot set `display, position, top, left, width, height, min/max, grid, flex, padding, gap, margin, z-index, pointer-events, transform` or replace DOM/audio handlers. Appearance can set material gradients/colors/shadows/texture/text, decoration, and dimension-matched artwork slots. Border thickness should remain geometry-owned; border color can be tokenized. Avoid `@layer` before resolving legacy unlayered `!important` cascade.

### Visual acceptance: DO NOT DEVIATE

- Chassis/rail/toolbar/channel metal: satin silver, modest brushed grain, clear legible darker lettering.
- All aluminum buttons have **unchanged face geometry ON/OFF**. Only ring/ink/glow change; no pressed-in displacement; micro-bevel proportional to size.
- **FX insert + FX power + FX workspace are glossy blue-black glass, NEVER brushed aluminum**, including bypass/empty states.
- MUTE red; SOLO amber; HPF green; phase selected distinct blue.
- FX controls and MASTER remain consistent across pages; dB channel/MASTER displays line up.
- PAN center-zero, blue line from center to thumb, original PNG thumb in front; original fader artwork including red MASTER cap unchanged.
- Compact 12 input + 1 MASTER layout belongs to GUI source, not tokens.

## Optimized execution protocol

**Next executable work is T0, NOT T5.** Branch off **latest** `main` and create isolated PR. Keep this documentation draft separate. Record baseline screenshot and computed styles for MIX (at 1680×920, 1440×820, 1280×768, 1100×740), FX RACK, SIGNAL FLOW, plugin modal, and native controls if available. Capture bounding boxes for PAN, Ø, HPF, MUTE, SOLO, dB display, fader and MASTER. Inventory CSS owner conflicts by property.

Then implement T1–T5 as independent PRs:
- **T1:** Extract active material declarations from legacy CSS to component owner with **no default visual diff**. Do not "cleanup" CSS mechanically by deleting all `background` or `!important`; audit actual cascade.
- **T2:** Migrate precise metal/glass visual values **from the preserved adapter** into semantic token consumers. Default remains appearance-identical to current approved main.
- **T3:** Build-time manifest validation and static precompiled token CSS. Deny arbitrary remote CSS/scripts. Theme pack cannot carry layout declarations.
- **T4:** Root `data-armix-theme`, early startup, safe storage and fallback; no `renderMixer`, audio bridge `send()` or component reparenting on switch.
- **T5:** Compact keyboard-accessible Appearance chooser in app menu, not the audio SETUP button. Avoid toolbar collision.

Optional **T6** introduces only dimension/hotspot-equivalent fader/PAN skins (canonical fallback). **T7** validates Windows WebView2 origin, startup and DPI 125/150%, without calling UI tests evidence of DSP audio stability.

One issue or PR per gate. After any GUI change run:

```bash
python tests/theme_reference_fixture_test.py
python scripts/build_ui_css.py --check
node --check web/app.js
python tests/button_asset_test.py
python tests/pan_button_asset_test.py
python tests/fader_asset_test.py
python tests/gui_baseline_lock_test.py
python tests/ui_smoke.py
```

Also run all new cross-theme geometry and behavior tests after T2. Native Windows CI should pass before merge. Golden visuals and physical Windows DPI acceptance remain separate checks. If current `main` changed while working, compare and rebase small commits; never force-push stale files over new main.

## Handoff definition of done

A new engineer has succeeded only when users can choose **Studio Default** / **Brushed Metal** inside the main app; appearance switches without reload or engine effects; preference survives supported restarts; CSS compiler/manifest is deterministic and validated; default is unchanged; approved silver/button/FX glass look matches the preserved reference; across 13 strips every important control preserves x/y/size and pointer behavior. Until these are demonstrable, mark theme engine **NOT IMPLEMENTED**.

**Anti-naive check:** do not solve theming by toggling two gigantic standalone HTMLs, conditionally rewriting all stylesheet rules, global button selectors, mass `!important` overrides, CSS geometry tokens, or rebuilding mixer DOM on every switch. All are considered architecture failures.
