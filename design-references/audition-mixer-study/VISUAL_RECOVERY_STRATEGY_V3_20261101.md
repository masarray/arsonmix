# ArSonMix Preview V2 — Visual Recovery v3 / Asset Reuse Contract
**Date:** 2026-11-01 · **Owner:** isolated reference GUI / PR #278 · **Status:** implementation specification; NOT yet visual acceptance.

## 0. Decision and authority

This decision follows the owner's actual screenshot `image(20261101-080701).png` (1817×1005 browser frame), PRD v5.2 §9, ARD v5.2 §K, and the existing *approved native GUI asset pipeline*. The owner accepts the general single-window LIVE-console workflow, but **rejects the current visual styling, density, eye fatigue, insufficient precision and artificial CSS slider art**. No new product design, DAW timeline, reworked native DSP or another mock audio runtime.

**Design direction:** **Adobe Audition** for clean vertical channel hierarchy + section show/hide; **Studio One** for compact rack grouping / quick FX selection and console operations; **Waves eMotion LV1 v17** for operator-first fader focus, layers/modes and customizable information density. **ArSonMix original artwork** is the source of material identity. These are workflow inspirations, not a mandate to copy a third-party application’s copyrighted skins.

**Critical safety:** PR #278 and `/preview-v2/` remain browser-only demos. There is no verified real VST3 instance, native meter, or live PCM. UI state changes cannot claim ACK or hardware changes. Keep PR Draft.

## 1. Screenshot diagnosis — what is wrong, why it feels wrong

| P | Visual observation in screenshot / audited DOM | Root cause | Required correction |
|---|---|---|---|
| P0 | Every strip shouts equally: routing labels, add-effect rows, toggles, tiny EQ graphs, pan and TRIM occupy the same visual weight. | No visual information budget / no priority distinction. The expanded rack across **all** channels multiplies the same empty controls. | **Fader first**, one strongest readout, one nameplate, one row of MUTE/SOLO; convert idle FX/EQ/SENDS to short summaries. Provide *mode/expanded* access on demand. |
| P0 | Main fader thumbs appear short and repeatedly metallic-painted; PAN thumb looks like a separate tiny widget. | Preview V2 invented CSS-drawn **48×27 px** fader thumbs and CSS **15×19 px** pan thumbs, replacing the approved PNG artwork. | Restore the **natural-size original 36×77 px** ARI thumb and **18×40 px** PAN-SLIDER exactly; retain actual cavity and center line. Do not recolor, stretch or apply filters at runtime. |
| P0 | Fader meter/scale arrangement and V2 dB limits are not the previously verified physical ruler. | V2 CSS grid `[scale][fader][meter]` and `+15…−60` differ from old verified `[left VU][fader][right ruler]` and native reference `+10…−60`. | Restore geometry and two independent calibration rulers; reconcile fader gain range with **actual native contract** before declaring parity; do not silently force `+10` if engine API differs. |
| P0 | Plugin Manager obscures several middle/right channel strips, even if Master is still visible. | `position:absolute` overlay is not allocated space in the bank viewport. | Desktop **docked inspector/drawer inside same Mixer window**, 300–340 px, **reserving bank width** when open; pinned MAIN stays visible; no overlay of any fader. Bank capacity recalculates only on drawer open/close or viewport resize. |
| P1 | Color accents repeat on every strip in fader track, number, trim, FX and footer; metallic grooves and gradients compete for attention. | No budget for bright elements; highly repeated panels/strong strokes create visual rhythm fatigue. | Use quiet graphite/steel surfaces; one meaningful accent per channel (nameplate + under-cap line); reserve red for clip/mute, yellow for solo, cyan for focused/active. Lower bezel and gradient contrast 40–60% from V2 visually; no gratuitous glow. |
| P1 | `1/8`, `+1 Add effect`, `+2 Add effect`… appear in many strips, and empty EQ graphs look as important as working channels. | Empty-list affordances repeated in each slot; static equalizer decoration in inactive channels. | Rack summary **`FX 2`**; when opened show occupied slots and **one `+ Add FX`** on first empty row; advanced slots via `Show all`. Show **EQ** label/active flag; curve only in EQ layer or selected channel, never decorative repeated waveforms. |
| P1 | Top bar contains dense labels and small `More`/bank/scene controls. The 150 px "Comfort" width alone does not fix readability. | Visual density was scaled by column width without redistributing information. | Use 2-tier hierarchy: always visible **source→MAIN, MUTE/SOLO, PAN, fader/meter**; FX/SENDS/EQ details are deliberate secondary layer; infrequent controls in one top More menu. |
| P1 | Tooltip-sized hints/abbreviations and numerous ~9–10 px labels are uncomfortable for seniors. | Too much metadata exposed at all times. | Body primary >= **12 px** on Comfort; key channel name >= **13 px**; dB readout 16–18 px; secondary >= **11 px**, meet WCAG 4.5:1 normal text where applicable; direct actions >= **24×24 CSS px** or equivalent WCAG spacing; prioritize ~30 px actions. |
| P1 | FX/SEND/EQ expand/collapse formerly misaligned and fader well grows/shrinks unpredictably. | Repeated CSS overrides across old base, media blocks and late `!important` patches. | **One source of truth** for section-height tokens per density, every rail/track/Master aligned; one relevant layer at a time by default, preserve independent explicit toggles. Measure computed bounding boxes in Chromium. |
| P2 | 3 density variants feel like 3 independently styled themes. | Repeated CSS selectors and conditional overrides. | Density controls **only size/spacing**; all roles use same component structure, source artwork and state palette. |

## 2. Restore the actual existing ArSonMix assets — exact source inventory

Assets **already checked into `main`**; do **not** draw replacement CSS thumbnails, search for third-party skins or create new derivative PNGs:

| UI part | Exact canonical asset | Geometric contract | Code ownership/reference |
|---|---|---|---|
| Ordinary INPUT/Bus/FX fader | `web/assets/faders/FADER-ARI-SILVER.png` | 36×77 RGBA, render **natural dimensions**, black center detent at **y=38** | `web/styles-src/41-mixer-performance.css`, `42-mixer-premium.css`; `docs/ARI_FADER_ASSET_20261026.md` |
| Pinned MAIN fader | `web/assets/faders/FADER-ARI-ROSEGOLD.png` | 36×77 RGBA, same detent and travel | `docs/MIXER_10CH_ERGONOMIC_PASS_20261026.md`. **Use ROSEGOLD (not bright GOLD) unless owner explicitly prefers otherwise.** |
| PAN thumb | `web/assets/pan/PAN-SLIDER.png` | 18×40 RGBA, centered track and center detent, center-origin direction ribbon | `web/styles-src/40-rail-pan.css`, `docs/PAN_AND_BUTTON_UX.md` |
| MUTE/SOLO small physical buttons | `web/assets/buttons/SO-S-SLV.png`, `SO-S-RED.png`, `SO-S-YLW.png` | Preserve assets/aspect ratio and correct OFF/ON semantics. Touch *target* can be larger than visible PNG; never stretch into FX rows. | `web/styles-src/20-button-180.css`; `docs/PAN_AND_BUTTON_UX.md` |
| Fader recess, LED segments, tick calibrations | Existing authored CSS (no new PNG necessary) | VU bank **left**, physically calibrated fader **center**, dB ruler **right**, track dark **above cap**; RAIL3 cyan onset **below cap** with black falloff at bottom | `docs/FADER_MODULE_REFERENCE_AUDIT_20261027.md`, `docs/RAIL3_CYAN_FADE_TO_BLACK_20261031.md` |

**Asset integrity note:** Historical docs have more than one vintage for ROSEGOLD and GOLD artwork. Verify **Git blob/decoded PNG bytes in current main**, image dimensions, hash and visual reference before pinning SHA; don't trust an old document hash without reconciling it. No image files/fonts exported from the system.

**Architecture:** Create a reusable **presentational** `ArsonFaderArt` and `ArsonPanArt` component or equivalent local module in `design-references/audition-mixer-study/`. Import only the **visual behavior/material**; do not copy old runtime audio controls or event handlers. The current standalone `index.html` and `/preview-v2/` must render the same bytes; bundle local PNGs under `site/preview-v2/assets/` using a deterministic build step (or one build-time data URI per asset; not repeated per strip). Preview **must** work offline when its assets are downloaded as a bundle. Zero third-party CDN or font requests.

## 3. A readable 150 px Comfort strip — visual hierarchy and information budget

From top to bottom; all channels line up at the same vertical band boundaries:

1. **Identity / ROUTE (priority 2):** one high-contrast channel name `VOCAL 1`; small Mono/Stereo flag; compact **IN → OUT** (e.g. `IN1 → MAIN`); detailed device path on hover/selection, never truncate a *critical* output without accessible full name. 1 line source + 1 line output maximum in normal mode.
2. **PROCESSING SUMMARY (priority 3):** compact `FX 2 · EQ · SEND 1` with status marks; **not** eight identical `Add effect` rows or full mini EQ graphs simultaneously on all strips. Show an active FX *name* when focused, not every long plugin title in every strip. One explicit global **ROUTE / FX / SEND / EQ** section rail mode, which reveals the relevant expanded band's controls across the bank while preserving fader alignment.
3. **PAN (priority 2):** one 18×40 original PNG slider; center snap; light band exclusively from C→left or C→right; full range accessible via keyboard; tiny L/C/R markers or value on focus, not multiple duplicate numbers.
4. **MUTE + SOLO (priority 1):** clear, >=30px high hit regions in Comfort; PFL always accessible but visually lower priority; never hide a critical safety action behind a More menu.
5. **FADER + LEFT LED + RIGHT GAIN SCALE (priority 1):** allocate the largest continuous area (target **>=48%** of visible strip height at 900px desktop; **>=260px** usable travel area when viewport permits). Single dB LCD just above fader, meaningful clip indicator. Reuse actual silver/rose-gold artwork; cap detent and scale must agree at **0, minimum and maximum**.
6. **Footer ID (priority 2):** high-contrast nameplate; one thin semantic channel-color strip only; status role/icon stays readable, no large saturated full-column backgrounds.

**Initial view:** `Comfort + Mix/Fader layer`; show full faders, live meters, names, route summaries, pan, Mute/Solo/PFL. **FX/SENDS/EQ collapsed** by default. Expand a layer explicitly on left rail: on selection of FX, present per-bank real occupied inserts; avoid simultaneous large FX + Sends + EQ layout unless the operator explicitly opts into Expert/Expanded mode. Do not silently change any audio gain on show/hide.

### Concrete width budget (Comfort 150px)
- Left VU bank **28px**, fader well **54px**, right gain ruler **25px**, two inter-column gaps **6px**, horizontal padding **8px** each side. Total **135px**, leaving **15px tolerance** for borders and scrollbar.
- Normal fader thumb stays **36×77 px** even in Comfort; achieve better usability by enlarged **invisible hit target**, not bitmap stretching. Maintain thumb black-center y=38; use the same center point to map pointer, keyboard and native gain readback. Native max/min must be the authority (current V2 demo `+15`, legacy reference `+10`: explicit unresolved mismatch).
- Meter is real fixed-zone green→yellow→red **only when PCM is available**; simulated animation stays clearly marked DEMO in browser preview.

## 4. Non-negotiable behavior and performance

- **Do not rewrite accepted Console UX:** Keep bank switching, role filter, pinned MAIN, per-strip input/output, mono/stereo processing independent of physical source, FX search/insert/reorder/bypass, Sends on Faders, cue/PFL, Snapshot Safe, copy/paste and channel selection.
- **Plugin Manager should not cover any active fader**: use an in-window docked panel with bounded width, independent scroll and a stable pinned MAIN. Opening closes no audio path and changes only bank geometry. `Escape` returns focus to invoking FX slot.
- Readability must not depend on hover, differentiating colors alone, mouse precision or visual memory of hidden modes. Preserve ARIA names, keyboard fader/pan + Enter/Escape, focus outlines, explicit OFF/ON.
- **No heavy repaint:** native-to-UI level/PCM meter updates touch only affected custom properties/text nodes; static original image is drawn once; no canvas repainting for a static fader, animated blur, image filter, per-strip requestAnimationFrame or whole DOM rebuild when dragging.
- **One authoritative source:** `design-references/audition-mixer-study/index.html` stays isolated reference; `web/**`, native bridge, engine and build are owned by their workstreams. Do not merge this DEMO as a real audio mixer.
- Pages `/preview-v2/` is a **snapshot** from reviewed PR #278, not a magic live branch view. The next visual milestone must update it via the **existing single Pages publisher** only after explicit source/visual QA; preserve `/preview/`.

## 5. Meaningful delivery milestones, not micro-PRs

### V3-G1: Reuse old hardware art **and calibrate**
In one batch replace new CSS fader + PAN with approved PNG components and restore VU-left/fader-middle/gain-scale-right, old RAIL3 cavity, silent-above-cap. Keep visual hit targets >=24px; no old runtime/DSP code. Verify image asset bytes, native fader contract and pan center snap.

### V3-G2: One clean surface + plugin drawer
As a **single integrated UI slice**, remove repeated dummy FX/EQ rows, implement one active upper-mode layer, simplify top bar, maintain FX/SENDS/EQ accessibility, make Plugin Manager dock rather than cover faders, preserve horizontal bank/MAIN. All relevant controls remain discoverable within two deliberate interactions; Live Fader/MUTE/SOLO always one interaction.

### V3-G3: Visual qualification, then **one** public Preview V2 update
- Browser pixel and bounding-box captures **1366×768**, **1680×900**, **1920×1080** at 100% zoom.
- For each density, test **Mix default**, **ROUTE expanded**, **FX expanded**, **SENDS expanded**, **EQ expanded**, **Plugin Manager open** and **Master pinned**. Ensure no overlaps, header wrap, clipped text, or hidden action; 10+ channels bank/scroll predictably.
- Reference snapshot test: real PNG must decode 36×77, PAN 18×40; fader-detent aligns with right ruler at 0/min/max and doesn't jump on pointer-down. Mono VU one physical meter, stereo separate L/R and clip; dBFS scale not confused with gain scale.
- Keyboard/interaction functional gate: pan center, FX reorder, mute/solo/PFL, Snapshot Safe, sends-on-faders preserving primary fader, plugin search and Escape; no regression to current 84/84 DOM checks.
- Accessibility: contrast >=4.5:1 for ordinary text where WCAG AA applies, core pointer targets >=24×24 or qualified spacing exceptions, 200% text zoom without losing *critical controls* as feasible with intentional banking.
- Performance: no continuous CSS animation in idle, no per-frame DOM replacement; compare pointer/fader responsiveness on integrated GPU and ordinary Windows machine before claiming performance PASS.
- Update `site/preview-v2/` **only once** after V3-G1/G2 pass; keep exact source SHA in `build.json`, run a single targeted Chromium screenshot workflow, and publish via the existing guarded main Pages pipeline. No new release/Windows CI merely for CSS.

**Release gate:** User visual signoff only on an *actual rendered Preview V2 screenshot*, not CSS source diffs. Rejection means targeted redesign of the failing row, not new visual theme or whole-app rewrite.

## 6. External engineering references

- Waves official eMotion LV1 v17 (2026-09-01): https://www.waves.com/emotion-lv1-v17-news — fewer distractions, configurable mixer rows, focused parameters.
- Waves official LV1 Mixer window tutorial: https://www.waves.com/emotion-lv1-tutorial-4-1-mixer-window-overview — layers/modes and always-available faders/meters.
- Adobe official Audition mixer routing/EQ: https://helpx.adobe.com/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html — Show/Hide Inputs/Outputs, Effects, Sends, EQ.
- Studio One console reference: https://www.soundonsound.com/techniques/using-studio-ones-mixing-console — Console, sections, scene view and visibility.
- WCAG 2.2 text contrast and pointer targets: https://www.w3.org/TR/WCAG22/ and https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum
- Internal approved art/calibration: `docs/ARI_FADER_ASSET_20261026.md`, `docs/RAIL3_CYAN_FADE_TO_BLACK_20261031.md`, `docs/FADER_MODULE_REFERENCE_AUDIT_20261027.md`, `docs/PAN_AND_BUTTON_UX.md`.

> **Implementation checkpoint (2026-11-01):** V3-G1 original silver/rose-gold fader PNGs + pan PNG and VU-left/fader-middle/ruler-right layout are now implemented in PR #278; V3-G2 quiet default FX/SEND/EQ, **true paged 10-channel bank**, arrow-only navigation, hidden bottom scrollbar, and Plugin Manager reserved-width dock are implemented. A single Pages commit `44ff625` copied the reviewed source to `/preview-v2/`. The stable HTML/JS DOM regression suite is **98/98 PASS**. **V3-G3 rendered Chromium screenshot/geometric QA and native PCM/VST3 qualification remain unverified.** Historical old fader `+10 dB` vs V2 demo `+15 dB` scale is NOT reconciled with production native gain limits; the preview currently keeps its demo `+15 dB` control range and must not be called calibrated native parity.
