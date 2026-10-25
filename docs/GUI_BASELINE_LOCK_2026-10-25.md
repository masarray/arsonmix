# APPROVED GUI BASELINE — ArSonMix v0.8

**Status: LOCKED / APPROVED by product owner — 2026-10-25.**

This is the canonical cross-thread, cross-branch GUI checkpoint. It is a **visual/interaction design baseline**, not a Windows-native VST3 release or audio conformance statement.

| Reference | Value |
| --- | --- |
| Approved design commit | `343692fc9b702661a8455cca30f0ffd108907324` |
| GUI implementation PR | [#2 — merged](https://github.com/masarray/arsonmix/pull/2) |
| Merge commit into `main` | `aff95a7c811a3817edf61303e1f24ef31e53d11d` |
| Archived reference branch | `checkpoint/gui-v0.8-approved-20261025` |
| Canonical integration base | **Latest `main`** (never the old preview branch) |
| Preview | https://masarray.github.io/arsonmix/preview/ |
| Published revision metadata | https://masarray.github.io/arsonmix/preview/build.json |
| Verification | [Chromium UI smoke green](https://github.com/masarray/arsonmix/actions/runs/40166477815) |
| Native VST3 integration | **SEPARATE / NOT QUALIFIED BY GUI MERGE** |

## Locked user-approved appearance and interactions

1. **Composition:** one title/device/action toolbar only, no redundant preview header. Left rail compact, clear channel list, sound presets, snapshots and readable SAVE SNAPSHOT / SAVE FX / EXPORT actions. No empty buttons or large dead space.
2. **Main surface:** 12 colored source strips + stereo red **MASTER as the thirteenth aligned strip**, one uninterrupted selection outline through each channel. Channel header, fader start and footer baselines match on EASY and PRO at tested sizes. Click channel identity to rename/change icon/color.
3. **Primary workflow:** six **visible** compact VST FX inserts on every input and master strip. Separate power/bypass control and FX-name editor action; readable names, no `◉` insert status clutter or repeated +.
4. **Hardware feel:** source metallic silver fader caps, distinct red master cap, true relative drag without click-jumps, no unprofessional glow, control responsiveness, contextual metal button artwork (small toggle vs nav vs preset). MUTE text never changes; red MUTE state is semantic.
5. **PAN:** metallic horizontal slider with L/current/R text and center snap, including master. No redundant PAN heading; restrained green center-to-thumb indicator.
6. **Meters:** per-channel peak hold/clip latch, distinct master L/R meters with peak hold. Compact master streaming reference values: **LUFS-I + dBTP only**, with legible numerals and **SIM** status in preview.

## Source and parity

`web/app.js`, `web/styles.css`, `web/index.html`, `web/assets/`, `AR-MIX-Live-FX-Host-Standalone-v0.3.html`, `site/`, and `tests/ui_smoke.py` from approved commit are the visual implementation. The standalone filename `v0.3` is an inherited path (not the approved GUI version). Avoid renaming until the native embedded resource path is migrated and tested.

**Do not overwrite files wholesale from an older engine branch.** Pick up fresh `main`, make narrowly-scoped adapters and preserve CSS artwork, strip geometry, routing controls and state restoration. If tests or a screenshot from a native PR looks different, it must be reconciled to the approved baseline, not treated as a new approved design.

CI required for any GUI merge:
- JavaScript syntax validation.
- Asset byte/dimension and standalone parity tests.
- Playwright Chromium smoke for the 13-strip master, 6 FX slots, readouts, PAN/fader behavior, channel naming, mute/solo, clip state and FX bypass.
- Visual QA at 1680×920 and 1440×820. Test additional Windows DPI 125%/150% before treating a native WebView2 build as production-ready.
- No fabricated assertions about native processing; hand off bridge events to actual DSP.

## Engine/native reconciliation ledger — DO NOT accidentally close unfinished work

Native stack PRs [#1](https://github.com/masarray/arsonmix/pull/1), [#3](https://github.com/masarray/arsonmix/pull/3), [#4](https://github.com/masarray/arsonmix/pull/4), [#5](https://github.com/masarray/arsonmix/pull/5), [#6](https://github.com/masarray/arsonmix/pull/6), [#7](https://github.com/masarray/arsonmix/pull/7), [#8](https://github.com/masarray/arsonmix/pull/8), [#9](https://github.com/masarray/arsonmix/pull/9), and [#10](https://github.com/masarray/arsonmix/pull/10) are sequential native engine changes, many drafted against other feature branches. Integration [#11](https://github.com/masarray/arsonmix/pull/11) still references **older GUI v0.5/commit `7d489203`**, not this approved GUI. PR #11 must switch the visual source of truth to merged `main` and keep authentic audio behavior (no fake VU, actual FX bypass/PAN/native chain/routing, protected audio callback and proper plugin state). Merge the engine stack **only after** independent Windows CI and real-device checks. Merely closing PRs without merging validated changes would silently discard work.

## How future threads should proceed

1. Read `README.md`, `AGENTS.md`, this document, `docs/GITHUB_PAGES_PREVIEW.md`, and the current PR/CI state.
2. Work from new `main`; reference this checkpoint branch to compare against regressions. GUI improvements require explicit user approval before changing the locked visual language.
3. For native integration, diff by function instead of overwriting `web/app.js/styles.css`; keep both native WebView2 control-contract tests and GUI Chromium tests in CI, plus Windows hardware evidence.
4. Publish Pages from main and verify that deployed `build.json.sha` matches the **tested** commit (not a stale branch).

**Acceptance statement:** the owner said the GUI was already "rapi dan bagus" and requested this progress be locked and merged. These exact visuals and interactions are the default for every subsequent thread.

## Post-checkpoint native integration annotation
The approved visual checkpoint above is unchanged. After this lock, [PR #13](https://github.com/masarray/arsonmix/pull/13) integrates experimental P2.5 native JUCE audio into current `main` by preserving the exact v0.8 CSS/layout and only adding the native bridge, a browser-preview meter guard, and backend sources. This is **not** physical hardware FAT or a stable release. Older references to PR #11 being unfinished describe the prior pre-integration state. Future work starts from `main`; the historical `checkpoint/gui-v0.8-approved-20261025` branch remains recoverable.

## Requested native workflow cleanup — 2026-10-25

The GUI v0.8 mixer geometry, colors, 13 strip layout, metallic faders, PAN and buttons remain locked. Product-owner-requested navigation cleanup is implemented in [PR #16](https://github.com/masarray/arsonmix/pull/16) and must remain compatible with native engine integration: native SETUP opens JUCE ASIO/WASAPI selector directly; PLUGINS, MY SOUNDS and SIGNAL CHECK are primary toolbar actions, never buttons appended to the status bar. The non-DSP parametric EQ demo editor (including strip double-click routing) and its styles are deleted; fader double-click restores unity gain. The overlapping `.rack::before` FX CHAIN label and fake toolbar CPU graph are gone. Footer contains telemetry only. Standalone preview must retain matching HTML/CSS/JS with honest desktop-only tool feedback. All CI plus Windows hardware acceptance remain necessary before treating this as validated behavior.

## GUI usability refinement — 2026-10-25

Explicit product-owner follow-up: PRO/EASY mode selector is removed; one stable optimized console layout remains. MASTER gains a dedicated fixed 168px width with expanded dual L/R meters, while source strips flex for available space. Vertically unify 97px master/source controls and 41px action rows so MUTE, SOLO and non-clickable master STEREO badge have identical centers. Channel identity popover anchors above the clicked channel strip name with a clear pointer, highlighted source strip and no full-screen blur. Header is regrouped into left device/session context and right evenly sized, vertically aligned seven primary tools. These are refinements to the existing locked metal hardware aesthetic, not a request to replace it. Layout regression tests cover 1680x920 and 1440x820; physical Windows DPI/driver acceptance is separate.

## Crystal Candy 3D control refresh — 2026-10-25
User-directed styling change supersedes the previous stretched PNG **button** appearance, not the metallic fader/PAN hardware assets. Header navigation, sidebar tabs/presets/snapshot controls, channel MUTE/SOLO, HPF/Ø, A/B sends and master action buttons use compact layered-glass CSS jewel gradients. OFF = polished steel-blue, selected nav and Send A = cyan, selected tabs/HPF/save FX = emerald, MUTE = coral, SOLO = amber, and phase/send B = violet. Shared CSS `--cc-*` tokens and 70–80ms tactile hover/press animation avoid extra JavaScript or realtime audio work. PNG button skins are no longer included in runtime stylesheet; original 31×90 metallic fader and 18×40 PAN thumb remain intact, as do all strip dimensions, routing, WebView2 and JUCE native DSP. CI validates CSS-only skins, semantic toggles, preview parity, and original PAN/fader bytes. Historical button PNG files are not UI dependencies.

## 2026-10-25 — 3D Candy construction correction / selection glow

The earlier CSS-only button work was not faithful to the supplied SCSS's *extruded construction*. The canonical control face now uses the provided gradient model (bright top at 0%, shoulder 26%, colored body at 100%) with a real `::before` under-body offset 3px, a separate `::after` crystal reflection, 3-layer downward drop shadows, and a 3px physical `:active` depression. On press the lower base retracts to 0px as shown in the reference. Press/hover transitions remain 65–80ms CSS-only. All control color states remain semantic. Strictly preserve original PAN/fader PNGs, native DSP, button actions, hit targets and fixed MASTER width. Selected input/Master no longer use the hard 2px border or orange ruled lines; selected strips have a subtle translucent 1px internal seam with diffuse inset + outer tinted glow. Identity editor focus glow is similarly softened. Check computed `::before`, `::after`, pressed top/base state and selection border (0px) in Chromium; maintain HTML/CSS source parity. Do not regress visual alignment.
