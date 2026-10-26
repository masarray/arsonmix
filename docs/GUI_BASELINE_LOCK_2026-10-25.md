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

## User reference: Button 180 (2026-10-25)
The exact `button-180.html` uploaded by the product owner is the authoritative button-skin implementation. Previous speculative Crystal Candy gradient and pseudo-3D approximations are retired. Preserve the original seven-layer `box-shadow`, solid `background`, original `:active` 3px translate and depressed shadow sequence, 150ms transition, 3px radius, type and disabled behavior. Only semantic base/border colors vary, and padding/font size/letter spacing adapt to fit narrow hardware strip buttons without modifying mixer geometry. Apply the actual `btn-180` class to header, rail, source/master controls. Keep prior soft-glow channel selection, original metallic fader/PAN and VST3 engine unchanged. CI must assert precise shadow layers, 150ms, 3px press, solid computed background and standalone HTML parity.

## 2026-10-25 — Inter + recessed toggles + strip-name collision control
User screenshot audit: earlier Button 180 raised colored toggles used too-light gray OFF, bright-state white text, and 10px cast shadows overlapping the colored strip-name panel. The real Button 180 face/shadow model stays the source-of-truth, but persistent `.active`/`.selected` now latch at translateY(3px) with original `:active` recessed shadow; OFF returns to raised and dark graphite-slate. Active faces keep jewel colors from the existing mixer theme with ink-dark captions. 40px isolated fader-tools bays and 24px button faces leave a shadow clearance above the channel-name panel. The user-required InterVariable WOFF2 (SIL OFL 1.1) is embedded as a data URI in the canonical CSS, automatically included in JUCE BinaryData and the standalone HTML with no external font network dependency. Fonts apply consistently to all UI controls and displays. Original fader/PAN, 168px two-meter master, signal handling and plug-in host are unchanged. CI checks typography, state geometry, shadow clearance at 1440px and 1680px, and native build before merge.

## Fixed-size 3D toggle correction (2026-10-25)
The requested change is **only the Button 180 toggle depth**, not a size, font-size, alignment, height, padding or layout redesign. Restore the seven-layer **raised OFF** face and original **10px** button typography and A/B **31px bay / 27px button** geometry from the pre-PR27 approved Button 180 skin. Persistent ON has a dark **inset shadow** without transform displacement (`translateY(0px)` for both states). Mouse-down likewise retains the same element rectangle. The strip-name panel remains in front of any downward cast shadow via z-index, instead of changing button/bay height. Retain the embedded Inter font and color/contrast context. Regression checks compare x/y/width/height for OFF, pressed and ON, all 13 strips, and the 168px master. Do not reintroduce translateY(3px) or a 24px A/B button.

## 2026-10-25 — Left-rail pushable, A/B retirement, PAN layering
The left-rail action buttons (2 top tabs, 3 quick presets, 3 session actions) use the *user-supplied* three-span pushable DOM: `button.pushable > span.shadow + span.edge + span.front`. Preserve its three-dimensional negative-translate raised front, deep edge, shadow, hover and physically depressed active/selected appearance, adapted only to the narrow sidebar and dark-blue/teal theme. Do not apply this treatment to dense channel/snapshot selection rows or the main mixer buttons. Remove obsolete per-channel A/B button markup and `sendA/sendB` demo handler/state permanently; no usable routing/aux-bus model existed behind those labels. Source fader gains back their vertical space; MASTER retains MAIN L/R + CLEAR CLIP. PAN detent track `::after` must paint behind the original 18×40 metal PNG thumb: center z=1, fill z=2, thumb z=3. The thumb asset/data remains byte-for-byte unchanged. Preserve responsive layout, buttons baseline size, 168px MASTER dual VU, Inter embedding and standalone parity. Tests must assert shadow/edge/front structure, no sends, no collision, thumb stacking and keyboard/click workflow.

## 2026-10-26 — Unified Button 180 rail, darker OFF steel, exact PAN offsets
User rejected the earlier separate three-span pushable rail. The eight left-rail action buttons now use **exactly the same single-element `btn-180` 3D surface and CSS token scheme** as header and mixer buttons; the old `.pushable > .shadow/.edge/.front` markup, special geometry and CSS are removed. OFF steel is `#223541` with edge `#0c202c`; OFF hover brightens with a specular steel gradient and restrained cyan glow but does not change the button rectangle/transform, while bright active controls keep their functional palette and recessed inset shadow. Preserve Inter, 27px rail heights, master dual meter width, and A/B removal. PAN geometry MUST resolve to `.pan-track::before top:13px`, `.pan-track .pan-fill top:15px`, `.pan-track::after top:12px`; the original PAN PNG thumb remains at z-index 3 above the detent. No audio engine modifications.

## 2026-10-26 — Hover shine-only correction
The owner explicitly rejects any cylindrical/convex hover effect. The OFF hover must leave the **original solid rectangular Button 180 background, original seven-layer 3D box shadow, original 3px corner radius, x/y, dimensions and transform unchanged**. Only `filter:brightness(1.18) saturate(1.04)` and a slightly brighter border apply on hover. No hover gradients, expanded glows, substitute shadow stacks or new depth constructs. ON remains recessed and retains its contextual color, unaffected by OFF hover. Chromium regressions must compare the computed background image, exact box-shadow, border radius and rectangle before/after hover across left rail, header, and source channel.

## 2026-10-26 — Owner-approved black-crystal OFF + deterministic CSS source (supersedes earlier OFF steel)
OFF is now a **flat, compact, rectangular black crystal**, solid `#171c23` over `#070d14` edge with restrained upper inset light and deeper dark lower inset. No gradient dome or pseudo-body; 3px radius, geometry and hover coordinates stay fixed. ON remains the previously approved saturated colored recessed key, unmodified. The sole Button 180 authoring owner is `web/styles-src/20-button-180.css`; `scripts/build_ui_css.py` generates `web/styles.css` and the byte-identical standalone HTML for JUCE WebView2 and GitHub Pages. This replaces older OFF shadow/color instructions only, not PAN/fader/MASTER/audio contracts.

## 2026-10-26 — Button 180 invariant: OFF/ON identical physical construction (latest superseding instruction)
ALL Button 180 keys now use the **exact same inset box-shadow and transform** in OFF, selected/ON and mouse-down. No raised-versus-recessed difference or visual size change. Only semantic background/border/text colors differ: OFF neutral graphite `#292d34`, ON retains approved teal, coral, amber, green and violet mapping. Hover for both OFF and ON is brightness ONLY (`filter:brightness(1.18)`), with no border-color change, halo, extra pseudo-element or altered shadow. The latest owner's instruction supersedes older physically raised-OFF versus recessed-ON descriptions. CSS owner `20-button-180.css` and generated WebView2/offline HTML remain byte-identical.

## 2026-10-26 — Tactile dark-crystal cube Button 180 (LATEST visual override)
The owner requires OFF to read as a **raised squared black-crystal cube**, rather than a button already pressed into the panel. Use a restrained top/left glass reflection and a crisp layered lower-edge shadow; no convex gradient, outer cyan glow, added hover border, oversized radius, new pseudo-element or DOM layers. Hover strongly brightens the *same* material at `brightness(1.32)`, without altering the border or footprint. Pointer-down immediately recesses into the existing colorful-ON shadow treatment; persistent selected/ON retains the inset shadow and existing semantic colors. OFF and ON have deliberately **different tactile depth** but IDENTICAL element rectangles, offsets and 3px radius. This section supersedes the prior "identical physical OFF/ON" instruction, not locked fader/PAN/master dimensions. Shadows each have matching inset/outset slots for smooth 90ms interpolation. Tests cover the exact OFF→press→ON→OFF state machine at Chromium, incl. header, strip and rail.

## 2026-10-26 — Final Button 180 gray OFF colorway (LATEST; supersedes raised cube)
The user explicitly rejects the two different OFF/ON physical button skins. **Use the existing ON shadow, size, border shape, radius and optical treatment for both OFF and ON; ONLY the semantic color tokens differ.** OFF must use exactly `--b180-color: #858283`, `--b180-border: #636363`, and `--b180-ink: #2b2b2b` (managed centrally via `--b180-off`, `--b180-edge`, and `--b180-off-ink`). ON semantic colors stay unchanged; do not add any dedicated raised-OFF or pressed/selected shadow stack. Hover remains the already-approved brightness-only 1.32, no new border/glow. Width, height, radius, PAN 13/15/12, metal art, 168px master, audio and SCL etc unaffected. `web/styles-src/20-button-180.css` alone owns Button 180, generated WebView2 CSS and offline standalone must be identical. Chromium must check OFF/ON shadow shape equality (ignoring color) and exact OFF computed background, border, ink, even across toolbar, rail and strip.

## 2026-10-26 — Button 180 momentary tactile press (LATEST clarification)
The final OFF/ON **resting** materials must remain identical (same original inset/shadow structure, geometry, border and 3px radius), with user-approved OFF colors `#858283` / `#636363` / `#2b2b2b` and unchanged ON accent palettes. During an actual button press `button:active` ONLY, allow **1px `translateY`** and a deeper inset/shorter cast-shadow, fast 75ms transform / 90ms shadow feedback. This is a transient mechanical motion, never a persistent `.active`, `.selected`, `.on`, or `aria-pressed` skin. On mouse release, ON/OFF returns to precisely the same resting material and element location, with different colors only. Hover stays brightness-only 1.32 without new borders. Do not modify layouts/strip size, audio logic, PAN/fader assets or the canonical generated CSS/HTML parity.
