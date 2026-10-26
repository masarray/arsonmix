# ArSonMix Theme Engine — Implementation Ladder & Verification Strategy

> **DESIGN REFERENCE DELIVERED:** [Exact Brushed Metal adapter](../design-references/brushed-metal-v1.0/adapter.css) + [fingerprint](../design-references/brushed-metal-v1.0/reference.json) + [reconstruction script](../scripts/render_brushed_reference.py) + [integrity/parity test](../tests/theme_reference_fixture_test.py). Read [next-thread handoff](THEME_ENGINE_NEW_THREAD_HANDOFF_2026-10-26.md) **before T0**. The CSS should be *mined* for existing paints/shadows, **never copied** as the production runtime theme. T0–T7 statuses remain NOT IMPLEMENTED.

**Status:** READY FOR EXECUTION PLANNING · 2026-10-26 · implementasi belum dimulai.  
**ARD:** [ARD_MULTITHEME_ENGINE_V1_2026-10-26.md](ARD_MULTITHEME_ENGINE_V1_2026-10-26.md)  
**Scope:** Default vs Brushed Metal. GUI core menjadi layout owner tunggal. Native DSP tidak berubah.

## Working agreement untuk setiap engineer / future AI thread

1. **Fetch fresh `main` pada setiap PR.** Baca `AGENTS.md`, `docs/GUI_BASELINE_LOCK_2026-10-25.md`, `scripts/build_ui_css.py`, `web/styles-src/*`, `tests/ui_smoke.py`. Jangan cherry-pick HTML preview silver lama sebagai basis implementasi.
2. Edit `web/styles-src` sebagai source, **bukan** `web/styles.css` atau standalone langsung. Jalankan `python scripts/build_ui_css.py` lalu `--check`; JS standalone parity via existing repo build process. Jika menambah JS source terpisah, perbarui generator standalone secara deterministik **dalam PR yang sama**.
3. Default immutable selama migrasi: pertama capture current `main` screenshots/computed style sebagai golden baseline. QA menggunakan toleransi layout ≤0.5 CSS px untuk anchor penting, dan pixel comparison dengan mask area meter animasi / time.
4. **No change to engine/native** unless later PR specifically adds verified preference bridge; no DSP commands, no unproven crackle-free claims.
5. Satu concern per PR; no megarefactor, no global `button { ... !important }`; FX Rack black-blue glass wajib lulus.

## PHASE / PR ladder (dependency + Definition of Done)

| ID | PR scope | File owner / perubahan | Exit gate |
| --- | --- | --- | --- |
| T0 | Baseline inventory + rendering contract tests | `tests/theme_layout_baseline.py`, screenshot fixtures, CSS selector inventory report | Computed styles, geometry, MUTE state, FX glass, PAN/fader assets captured; **zero behavior change** |
| T1 | Extract *surface/material* declarations from legacy without changing outputs | `web/styles-src/10-console-legacy.css`, new `50-component-materials.css`; add to `scripts/build_ui_css.py` | Default screenshot/computed styles effectively identical, cascade debt reduced; no new blanket `!important` |
| T2 | Introduce semantic tokens consumed by component owners | `web/styles-src/20-button-180.css`, `30-selection-effects.css`, `40-rail-pan.css`, `50-component-materials.css` | All semantic states and Fx/glass roles mapped, **same geometry and default visuals** |
| T3 | Static token registry + compiler/validator | `web/themes/{manifest.json,*.tokens.json,theme-contract.schema.json}`, `scripts/build_theme_tokens.py`, generated `60-theme-tokens.css`, update CSS build | Deterministic output; CI rejects geometry declarations/assets dimension mismatch, duplicate IDs, invalid tokens, URLs |
| T4 | Theme manager + persistence + initial paint | `web/theme-manager.js`, prepaint bootstrap, standalone build parity; no `renderMixer()` | Switch Default/Brushed 100× without audio bridge sends, DOM rebuild, listener/timer growth, corrupt pref fallback |
| T5 | Minimal Appearance chooser | Small accessible popup from approved app menu/brand context, keyboard + focus; **not** audio SETUP | No toolbar wrap/collision across viewports; clear selected theme; persistent preference; open/close Escape |
| T6 | Fader/PAN alternate finishes (optional, guarded) | Approved image slots + per-theme asset registry; keep existing PNG as default | Assets pixel/hotspot geometry parity; center snap and fader travel unaffected; master remains red |
| T7 | Desktop WebView2 startup, storage and QA | If required, native preference only after storage/protocol confirmed; tests + docs | Restart persistence in actual profile; offline works; MSVC/Ctest/Chromium green; no audio interruptions |

**Optimized recommended merge sequence:** T0 → T1 → T2 → T3 → T4 → T5. T6 can be deferred; do **not** block basic theme switching on custom slider PNGs. T7 necessary before calling native release ready. If design/implementation review sees risks, split each further rather than batch all.

### T0 — Baseline contract and visual inventory (first PR, no styling changes)

Capture for 1680×920, 1440×820, 1280×768, 1100×740:
- 12 channel strips + one MASTER, column sizes, visible FX slots count (6 each), header/dB/fader/footer x/y.
- `getBoundingClientRect` of `.ch-id`, `.rack`, `.pan-track`, `.utility-row`, `.channel-actions`, `.db-display`, `.fader-zone`, `.cap`, `#masterLeft .master-db`.
- computed `backgroundImage`, `boxShadow`, `color`, `border` of OFF/ON MUTE, SOLO, HPF, nav, FX insert and FX power. Screenshot mask transient animated meters.
- plugin browser, channel identity popover, native plugin manager (as supported) and FX RACK. Accessibility audit focus, tooltip, contrast.
- audit `!important` inventory categorized by *property and selector*: active layout constraints / material candidates / unused historical. Do **not** delete unseen legacy declarations by guesswork.

Deliver artifact: `tests/fixtures/themes/default/` + JSON geometry fixture and deterministic CSS ownership report.

### T1 — Surgical ownership extraction (why first?)

Current `10-console-legacy.css` is large and mixes layout/material while `20-button-180.css` uses `!important`. Appending a theme CSS cannot be guaranteed to win. Move **specific material declarations only** from active legacy selectors into a central `50-component-materials.css` owner and replace them with `var(--am-...)` fallbacks. Preserve existing source-order semantics of geometry and same DOM.

Use assertion:
```text
For each migrated CSS declaration:
  existing selector/control -> intended component owner
  existing computed value   -> exact token fallback value
  inherited cascade         -> inspected in default state
  ON/OFF/hover/focus         -> inspected after migration
  screenshot diff            -> no unapproved change
```
No automatic regexp deletion of all `background` properties from legacy; there are critical physical graphics and states.

### T2/T3 — Theme specification and static validation

Define schema with `themeId`, `displayName`, `schemaVersion`, `engineMinVersion`, `tokens`, `assets`, `checksum`. Stable allowlist of semantic keys; only CSS value types valid for their token category. Missing optional token resolves to default; missing required token fails build. Allow CSS `background-image` gradients via controlled string grammar/build-time parsing. Block URLs, `@import`, nested selectors, `position`, `z-index`, `height`, `width`, `transform`, `display`, `content` from theme packs.

Manifest should be independent of `app.js` and `bridge.js`. No dynamic evaluation or JS from theme files. One generated static CSS with root token blocks, hashed/tested in CI and in offline standalone.

Semantic colors are not incidental:
- selected nav blue; MUTE red; SOLO amber; HPF green; Ø separate; negative gain not confused with clipping;
- FX bypass ON/OFF always legible in dark blue glass; FX insert must never turn silver in brushed theme;
- streaming `LUFS-I`/`dBTP` values remain legible; preview marked SIM where applicable.

### T4 — Controller state machine

States: `INITIALIZING → READY(default/theme) → SWITCHING → READY`, fallback `UNKNOWN/INCOMPATIBLE → READY(default)`.
- `resolve(themeId)` consults frozen allowlist; never builds CSS selectors from untrusted strings.
- `apply(next)` updates root attribute, emits application-only `theme-changed` event, and stores `armix.ui.theme.v1`. No audio send/render.
- persistence adapter catches `SecurityError`, quota failure and corrupt values; memory fallback for current session.
- native origin strategy: verify whether WebView2 uses packaged file URI, `NavigateToString`, or virtual host. Use native profile preference adapter **only if needed**; never send a theme ID through an unrelated audio command.
- prepaint resolution: inline tiny bootstrap or `AddScriptToExecuteOnDocumentCreated` in native, *before CSS first paint*; default fallback if unavailable.
- UI switching while active fader drag should preserve pointer capture and value; do not remove DOM nodes.

### T5 — UX chooser

Current toolbar has seven primary commands and native SETUP opens ASIO/WASAPI; **do not add eighth full-width button or hijack SETUP**. Place Appearance in compact application menu triggered by the brand/control approved in UX review. Menu shows two modest swatches:
- **Studio Default** (original dark crystal)
- **Brushed Metal** (silver hardware + dark FX glass)

Keyboard Enter/Space chooses; Escape closes; focus remains predictable; `aria-checked` or radio pattern. Theme applies live without spinner. Preference is not part of .json audio session import/export and does not trigger preset recall.

### T6 — Optional image skin assets

Maintain image dimensions and logical pixel mapping; original PAN PNG is 18×40. Fader cap artwork currently encoded with a separate 31×90 image and master distinct red. Render alt texture in same box, no `background-size` mismatch, no `transform:scale`. If the second theme lacks asset, keep canonical shared art. Validate SHA, dimensions, and byte-parity where required.

### T7 — Release validation

Native WebView2:
- browser and desktop both persist the chosen theme after close/restart;
- user changes live FX chain/audio state while theme toggles: no audio device restart, no callback reinitialization and no reparent/recreate plugin editor;
- product trace: theme event count independent of native `send()` count;
- manual FAT of Windows DPI 125%/150%; not a claim about audio crackle/FAT.

## Automated regression matrix

| Assertion | Default | Brushed | Switch ×100 |
| --- | --- | --- | --- |
| 13 strips, 6 FX inserts per strip | ✓ | ✓ | ✓ |
| Channel/master headers, dB/fader Y alignment | ✓ | ✓ | invariant |
| Four buttons equal box height/column alignment | ✓ | ✓ | invariant |
| PAN native thumb top and center fill | ✓ | ✓ | invariant |
| Fader mapping, no jump on drag | ✓ | ✓ | invariant |
| MUTE/SOLO/HPF/Ø + FX bypass state | ✓ | ✓ | unchanged |
| FX Rack dark glossy material | ✓ | ✓ | yes |
| No extra DOM nodes/listeners/native sends | ✓ | ✓ | strict |
| Plugin browser, popover, all toolbar commands | ✓ | ✓ | ✓ |
| Reduced motion, forced-colors, focus | ✓ | ✓ | ✓ |
| Invalid theme preference falls back | ✓ | ✓ | ✓ |
| Offline standalone parity | ✓ | ✓ | ✓ |

**Playwright pseudocode (contract):**
```js
const before = await snapshotGeometry(page); // header/rack/pan/buttons/db/fader/master
const audioOps = await interceptedNativeCommandCount(page);
await chooseTheme("brushed-metal");
const after = await snapshotGeometry(page);
expectClose(before, after, 0.5); // except non-critical text raster changes
expect(await interceptedNativeCommandCount(page)).toBe(audioOps);
expect(await page.locator(".strip").count()).toBe(13);
expect(await page.locator(".fx-power").count()).toBe(/* initially populated slots */);
expect(await page.locator("html").getAttribute("data-armix-theme")).toBe("brushed-metal");
// Repeat on FX RACK and SIGNAL FLOW, then restore default without reload.
```

**Important:** `aria-pressed`, meter readings, fader dB, edit selection and chain order must be snapshot-checked across switch. Use screenshot pixel baselines with dynamic-meters masked. State/DOM comparison stronger than only screenshots.

## CI gates / nonnegotiable

1. `node --check web/app.js` plus any new JS modules; lint build.
2. `python scripts/build_ui_css.py --check` and existing standalone byte-parity; if new JS modules exist, add parallel JS build/embedding parity check.
3. Existing `tests/button_asset_test.py`, `tests/pan_button_asset_test.py`, `tests/gui_baseline_lock_test.py`, Chromium `tests/ui_smoke.py`; update tests only in a way that maintains default appearance contract, not by deleting prior assertions.
4. New theme static contract check forbids geometry properties/global selectors/external URLs and validates manifest.
5. New Playwright geometry/performance/interactions matrix.
6. Native Windows MSVC + CTest/WebView2 launch + real DPI smoke (hardware where needed).
7. Review + product owner approval of both theme screenshots and Appearance menu prior to main merge. Preview from PR artifact; no competing Pages publisher; Pages remains main-gated.

## Failure scenarios / rollback plan

| Failure | Runtime behavior | Remediation |
| --- | --- | --- |
| Unknown theme ID | Default, no crash | Warn once; keep user audio/session intact |
| Theme token missing | Default token fallback | Build error for required tokens |
| Asset missing/invalid | Canonical art | Block release, record integrity error |
| localStorage blocked | Theme toggles for current session | Non-blocking notification optional |
| WebView profile changed | Default until preference restored | Verify native adapter storage |
| Device/PAN/fader behavior changed | Fails regression gate | Revert themed visual PR; no audio migrations |
| New GUI control added later | Uses default material token | Mapping contract/test flags missing role, rather than collapsing layout |
| CSS legacy specificity collision | Fail computed skin QA | Migrate corresponding declaration owner; avoid final override stacks |

**Stop condition:** If T1 migration cannot retain default computed geometry and button visual parity, **do not proceed to T2–T5**. Fix owner boundary first. Existing main/native functionality takes priority over shiny preview.

## Progress tracker (as of 2026-10-26)

| Work | Status |
| --- | --- |
| Source audit + ARD decisions | **DONE** |
| External design-token/CSS/WebView2 research | **DONE** |
| Implementation ladder and tests | **DONE — documented** |
| T0 baseline fixtures/code | NOT STARTED |
| T1 material ownership extraction | NOT STARTED |
| T2 semantic token consumers | NOT STARTED |
| T3 theme manifest/compiler | NOT STARTED |
| T4 live theme manager | NOT STARTED |
| T5 Appearance selector | NOT STARTED |
| T6 alternative hardware assets | OPTIONAL / NOT STARTED |
| T7 WebView2 runtime acceptance | NOT STARTED |

This plan is an architectural checkpoint; it does **not** claim the multi-theme engine has been built or validated.
