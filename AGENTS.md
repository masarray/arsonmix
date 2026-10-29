# Repository instructions — ArSonMix (all future threads and AI coding agents)

> **LATEST PRODUCT ENGINE / GUI — 2026-10-29 post-#159:** [R0B B3 #157](https://github.com/masarray/arsonmix/pull/157) is MERGED as `a9afddc` after exact-head Windows native CI [40890927975](https://github.com/masarray/arsonmix/actions/runs/40890927975) SUCCESS: NORMAL direct output is fail-silent without a valid device-bound 10CH+4AUX route, no CH1 legacy fallback on STOP/hotplug, with FX graph endCycle ACK/retirement preserved. [R2 #159](https://github.com/masarray/arsonmix/pull/159) is MERGED as `2acb509`: compact 10CH × 4AUX send matrix, PRE/POST+gain+pan shared inspector, CH→CH directed advanced routes, 32-send/DAG/duplicate preflight, ACK/DRAFT semantics, shared lazy VST picker; exact-head Chromium [40897545528](https://github.com/masarray/arsonmix/actions/runs/40897545528) SUCCESS. **DO NOT REPEAT B3 or R2 AUX SEND MATRIX.** Remaining: R0B #149 final selected-device safety, obsolete LAB alias reconciliation, real ASIO/WASAPI #15 physical FAT, full PDC, physical output patch, FX-equipped native Show recall and R3 UX/device acceptance. CI is not hardware certification.


> **CURRENT ENGINE DELIVERY — 2026-10-29 post-#156:** [R0B #156](https://github.com/masarray/arsonmix/pull/156) MERGED into `main` as `e62d377` — CH1 FX ADD/REMOVE, MOVE and existing preset recall share the native prepared graph handoff in both legacy and routed 10CH+4AUX callbacks. No routine JUCE audio callback detach for FX ADD/REMOVE; original VST3 instances and Orbit remain stable with de-clicked buffer-boundary publication, last-control-owner teardown and positive GUI ACK only after publication. Exact-head Windows MSVC/native 50/50 CTest, package verifier PASS [40871937375](https://github.com/masarray/arsonmix/actions/runs/40871937375). **DO NOT REDO R0/R1/R0A/G0/B1/B2.** R0B #149 remains OPEN: B3 one always-on verified/muted native session startup and remove user LAB/STOP split; #15 real ASIO/WASAPI zero-FX physical crackle FAT unqualified. Verify newest main and open PRs before work.


> **LATEST ROUTING STATUS — 2026-10-29 (post-#153):** Main contains Gate A ACK hardening via merged [#152](https://github.com/masarray/arsonmix/pull/152) (`72a4bfc`) and R0B B1 routed CH1 FX MOVE/de-click via merged [#153](https://github.com/masarray/arsonmix/pull/153) (`eb94203`, Windows native/RT tests and package verification PASS). #147 and #151 were closed as superseded; do not reopen or paste old UI. R0 premium workspace, R1 INPUT PATCH and R0A FX/Orbit remain delivered. **R0B #149 is still partial**: native unified startup, FX ADD/REMOVE/lifecycle, hardware #15 FAT not complete. Later R2/R3 outstanding. This paragraph overrides the older dated #147 OPEN assertions below; refresh live GitHub before coding.


> **ROUTING ENGINEERING HANDOFF (2026-10-29):** [docs/HANDOFF_ARSONMIX_ROUTING_R0_NEXT_THREAD_20261029.md](docs/HANDOFF_ARSONMIX_ROUTING_R0_NEXT_THREAD_20261029.md) is the concrete next-thread plan for integrated Routing R0, ACK PR #147, unified production R0B #149, INTERNAL/AUX R2 and real hardware R3. It complements, not supersedes, the global canonical reconciliation. Do not rebuild R0/R1 or regress FX/Orbit/audio callback.


> **LATEST RECONCILED LIVE STATUS — 2026-10-29 17:55 WIB:** Read [docs/CANONICAL_PROGRESS_RECONCILIATION_20261029.md](docs/CANONICAL_PROGRESS_RECONCILIATION_20261029.md) FIRST and refresh GitHub before coding. Verified main 4d14251: R0A #144, input patch #146 and premium routing workbench already MERGED; R0B production graph still OPEN as #149; ACK hardening #147 OPEN; zero-FX hardware crackle #15 OPEN. Older assertions that R1 is unmerged or all 10CH routing is absent are superseded. No visual/engine redesign from historical status snapshots.

**CURRENT NEXT-THREAD ENTRYPOINT (2026-10-29):** Read [docs/HANDOFF_ARSONMIX_NEXT_THREAD_20261029.md](docs/HANDOFF_ARSONMIX_NEXT_THREAD_20261029.md) before changes. Baseline #129 merged; GUI performance, FX atomic reorder/de-click, Orbit POLAR and P1I native 10CH LAB must not regress. Historic v0.8 12+1 references and dated PR OPEN statements further below are retained for lineage but not authoritative over current 10CH+MASTER main. Native full Shows/Snapshots is NOT delivered by localStorage scene UX. Source/CI/hardware evidence takes precedence.


**MANDATORY FOR ALL FUTURE MULTI-THEME THREADS (2026-10-26):** READ [docs/THEME_ENGINE_NEW_THREAD_HANDOFF_2026-10-26.md](docs/THEME_ENGINE_NEW_THREAD_HANDOFF_2026-10-26.md) and [design-references/brushed-metal-v1.0/README.md](design-references/brushed-metal-v1.0/README.md) BEFORE editing the theme system. The *exact* accepted Brushed Metal v1.0 styling lives in `design-references/brushed-metal-v1.0/adapter.css` (Git blob `1086c285a44d2d06787a12ae809c1d4c5c80bb44`; full hash in reference.json). Do not redesign buttons/FX visuals from scratch. Never paste the adapter as a production `!important` override: migrate its values into semantic tokens with GUI core remaining the only layout owner; FX insert/power/workspace stay black-blue glass. Follow ARD + T0–T7 PR ladder, preserve byte-parity and native bridge isolation. The runtime multi-theme engine is **not yet implemented**.

**MANDATORY: Read [docs/GUI_BASELINE_LOCK_2026-10-25.md](docs/GUI_BASELINE_LOCK_2026-10-25.md) before editing UI or reconciling VST3 engine PRs.**

**MANDATORY FOR NATIVE AUDIO WORK (2026-10-25):** Read the canonical [Smart Audio Engine ARD v1.0](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md) **and** [Smart Audio Engine implementation blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md), then review [P0 issue #15](https://github.com/masarray/arsonmix/issues/15). Native 0-FX Windows Audio crackling is hardware-confirmed; `main` is experimental, **not** live-certified. This new ARD supersedes conflicting claims in historical v0.3. Do not work on P2.6 12-channel DSP until P0 dry I/O acceptance.

## Approved visual contract
- GUI v0.8 approved by the product owner on 2026-10-25. Original exact source commit: `343692fc9b702661a8455cca30f0ffd108907324`; merged to `main` via PR #2 (merge `aff95a7c811a3817edf61303e1f24ef31e53d11d`).
- Permanent recovery reference: `checkpoint/gui-v0.8-approved-20261025` (never update or delete it). New contributions start from latest `main`.
- Maintain a 12-input + 1 master aligned strip console; 6 visible FX slots on each; master FX, PAN and stereo L/R; one top toolbar; compact left rail; fixed MUTE label; live insert power state; no-jump fader drag; contextual, realistically shaded controls and clear selected border.
- The streaming analyzer has **only LUFS-I and dBTP**. In the browser label values as simulated; do not invent actual DSP meter readings.
- Never copy stale UI files from the native branch over the approved GUI. Native integration must adapt the engine events and visuals to this baseline **without regressing GUI or changing its design without user request**.
- Source modules: `web/index.html`, `web/styles.css`, `web/app.js`. Self-contained offline distributable: `AR-MIX-Live-FX-Host-Standalone-v0.3.html` (historical name). Keep CSS/JS/HTML byte-for-byte synchronized with the standalone. `site/` supplies landing and review shell; `scripts/build_pages.py` copies the standalone.
- No universal button CSS override; preserve the original file assets and physical sizes. Native GUI changes must preserve layout and usability at 1680x920 and 1440x820.
- Run `node --check web/app.js`, all PNG/standalone asset checks, `python tests/gui_baseline_lock_test.py`, and `python tests/ui_smoke.py` before merging a GUI PR.
- GitHub Pages production deploys **from `main` after a green main Chromium smoke**. Do not create a competing preview-branch publisher.
- Native JUCE/VST3 PRs #1 and #3–#11 were **not** validated or merged by GUI PR #2. Do not mark them done or close them until native hardware, RT safety, actual meter/bypass and plugin tests pass. Reconciliation PR #11 is built from an older GUI and must be rebased/reworked against this lock.

## Strict audio-engine engineering rules (do not lose this across threads)

- P0 first: measure and isolate output-only vs capture-only vs dry-through with **zero VST3**, including backend-provenance telemetry. `callbackGapCount=0` is not proof of a clean output; do not call scheduler gaps true xruns.
- Default AUTO/STABLE profile: capability-aware and explained; direct zero-extra-FIFO path for single actual clock; cross-clock FIFO+drift estimator+ASRC only when proven necessary; cold-start/driver change rollback with generation tokens.
- Every audio path preallocated, bounded and allocation/lock/I/O-free in the host callback; third-party VST3 must be measured and vendor APIs respect proper thread affinity. No routine `removeAudioCallback/addAudioCallback` on FX insert/removal/reorder.
- One testable scope per PR. Deterministic fake clocks, bounded buffer/latency, device-state tests, Windows MSVC+CTest+Chromium PASS, actual sound FAT **separate**. Do not claim "optimized" merely because CI is green.
- Locked GUI v0.8 source untouched without explicit new visual approval. Engine status goes through native adapter. No stale branch bulk merges.
- Use `docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md` PR ladder and definition of done, and update it only with evidence/ADR when changing design decisions.
- Consolidate P0 infrastructure development and automated CI before requesting another physical hardware test; no user-assisted test after every tiny code edit. Native SIGNAL CHECK should expose **COPY FULL REPORT** to capture hardware capabilities, source-labeled xruns, budget histograms, and latest MARK CRACKLE events in the OS clipboard.
- Read [P0 Smart Audio diagnostics & prepared bridge](docs/P0_SMART_AUDIO_DIAGNOSTIC_AND_BRIDGE.md) before wiring an SRC. The `AdaptiveClockBridge` is **experimental standalone**, and MUST NOT be wired to the joined JUCE callback or claimed to fix physical crackling until separate-clock adapter feasibility, back-end xrun semantics, automated quality benchmarks and hardware FAT are demonstrated.

## P0 live dual-clock + recovery checkpoint (2026-10-26)

- **Current live engine truth:** [PR #35](https://github.com/masarray/arsonmix/pull/35) merged two actual independent Windows Audio shared capture/render JUCE callbacks with the prepared `AdaptiveClockBridge` in a render-master path; the prior text describing it as standalone applies only to older commits. This is an opt-in **BETA**, not automatic device qualification. Direct ASIO path has no SRC.
- **Required current handoff:** [P0 DIRECT restore recovery & next gates](docs/P0_DIRECT_RECOVERY_HANDOFF_2026-10-26.md). On an independent device loss, fail silent and restore the previous DIRECT setup only on the message thread using bounded generation-safe backoff; never hot-plug spin, auto-renable independent mode, or mutate devices in callbacks. Expose recovery history through native COPY FULL REPORT.
- The next P0 gates are stable endpoint identity, real AUTO/STABLE capability integration, backend render underrun provenance, long-drift/quality measurements, and one consolidated physical FAT. P0 #15 remains open and no blanket crackle-free claim is permitted. Other threads may concurrently change CSS: fetch latest main and never overwrite their GUI decisions.

## If you are a future ChatGPT thread
1. Fetch live repository `main`, this file and the lock document. Cite approved commit and deployment metadata accurately.
2. Check current native PRs / base graph before cherry-picking; do not accidentally erase more recent GUI or engine work.
3. Propose and implement only the requested incremental changes, validate actual behavior, and capture new checkpoints if another GUI revision is approved.
4. Distinguish *GUI-preview simulation* from *live native audio* in reports and releases.

## Post-lock native integration update (PR #13, 2026-10-25)
The **experimental** P2.5 JUCE VST3 engine has now been integrated into `main` using the approved v0.8 GUI as its ancestor; it does not supersede the visual approval or certify audio. Archived stacked PRs and the v0.5 integration PR #11 remain as lineage, not a source for overwriting the GUI. Work on later engine phases starts from latest `main`, tests with both Windows MSVC and the GUI lock Chromium workflow, and preserves all locked visual geometry. Do not merge a stale P2.5 branch wholesale back into main.

## 2026-10-26 CSS ownership: mandatory source workflow
- Edit `web/styles-src/` modules, not generated `web/styles.css` or standalone HTML. Run `python scripts/build_ui_css.py`; `--check` is required in CI and publishing.
- `20-button-180.css` is the **sole owner** of shared button skin and ON/OFF state. OFF is square solid black crystal with subtle upper inset reflections (color `#171c23`), ON remains approved vivid inset, radius 3px, no dome. Preserve PAN 13/15/12, 168px master and fixed button geometry.
- Historical rules quarantined in `10-console-legacy.css` remain candidates, not automatically dead code. Migrate only after DOM/cascade reachability and computed geometry assertions. See `docs/CSS_ARCHITECTURE_MILESTONES_2026-10-26.md`.

- 2026-10-26 latest visual contract: Button 180 OFF, ON, pressed, and selected use **one identical inset box-shadow** and no transforms or geometry differences. OFF neutral graphite `#292d34`, ON only changes semantic color. Hover in both states changes brightness only, not border. This explicitly supersedes historical raised-OFF instructions.

## P0 independent callback-silence watchdog (2026-10-26)
- P0 independent JUCE clock adapter detects missing capture or render callback progress by sampling existing atomics at 12Hz on the message thread. Three seconds of absent progress latches a fault and enters existing bounded DIRECT restore; not a driver-confirmed xrun.
- No new realtime clock reads, heap, blocking, workers, CSS/GUI edits. Callback-silence source and duration appear in COPY FULL REPORT recovery event. Physical FAT remains outstanding.

- 2026-10-26 LATEST GUI owner request supersedes prior identical OFF/ON depth: OFF Button 180 is **raised dark crystal cube** with crisp lower bevel + top glint; selected/ON and pointer-down are recessed with the existing bright contextual palettes. The element rectangle and 3px radius must never move; hover strongly brightens only (`brightness(1.32)`), with no new border. Edit only the CSS owner `20-button-180.css` and regenerate via `scripts/build_ui_css.py`. Preserve native DSP/PAN/fader/master assets and geometry.

## P0-02 ASRC signal integrity continuation (2026-10-26)
- Non-finite capture PCM is sanitized at independent FIFO ingress and counted without heap/locks; NaN/Inf latency and excessive FIFO capacity are rejected before allocation.
- `bridgeNonFiniteCaptureSamples` is source-labeled, not a confirmed hardware xrun. Deterministic spectral/injection tests cover 48k->32k passband, alias attenuation and finite output. See `docs/P0_ASRC_SIGNAL_INTEGRITY_2026-10-26.md`.

- **Latest 2026-10-26 UI correction (supersedes dark raised-OFF cube):** Button 180 OFF and ON must share *one identical optical/physical shadow recipe*; OFF differs only via `--b180-color:#858283;--b180-border:#636363;--b180-ink:#2b2b2b`. ON functional colors unchanged. No separate OFF or pressed shadow elevation, hover remains brightness-only 1.32. Edit the single CSS source owner, regenerate runtime CSS/standalone, run Chromium/native gates.

- **Latest Button 180 tactile clarification (2026-10-26):** OFF and ON **resting** geometry/shadow remain identical, OFF color tokens `#858283/#636363/#2b2b2b`. Only while `:active:not(:disabled)`, add momentary 1px `translateY`, compressed cast shadow and stronger inset (75ms transform / 90ms shadow), then immediately return to the shared resting appearance on release. Never latch depth on `.active` or `.selected`. Keep hover brightness 1.32, no hover border. CSS owner only, regenerate standalone, browser-computed tests for OFF and ON press.

- **2026-10-26 navigation ownership:** toolbar-actions and left-rail controls use the FX-power inspired `fx-nav` look, not Button 180. Edit `web/styles-src/21-navigation-fx-power.css`; never reapply Button 180 to those areas. Mixer channel/master buttons retain the approved Button 180 skin and tactile press. Exact icon FX power styling remains unchanged.

## 2026-10-26 — AUTHORITY: latest reconciled GUI, supersedes historical toolbar/rail guidance
- Read `docs/GUI_MERGE_RECONCILIATION_2026-10-26.md` before every GUI merge or CSS refactor. PR #53 **supersedes** PR #51's FX-power *appearance*: all seven toolbar-actions and eight left-rail buttons use **Button 19 grey-raised OFF / green-recessed ON**; `web/styles-src/21-navigation-fx-power.css` is a historical filename, not a request for FX-power optics. Never overwrite it from old PR #49 or old branches.
- PR #54's compositor/pointer and 2px main gutter improvements are locked into `main`. Preserve the latest 13-strip GUI and responsive spacing. Channel/master **Button 180** and real FX Rack `fx-power` remain their distinct control families.
- Before merge: check latest `main`, ancestry, changed files, and CI. **No GUI PR is outstanding** as of audited main `8d5f974`; old native stacked PRs are not safe bulk merges. Never replace current GUI from the old brushed-metal design branch, which is behind main.
- This authoritative reconciliation supersedes older `AGENTS.md` lines calling FX-power the latest navigation *visual style*. The legacy sections remain as history only.

## 2026-10-27 — LATEST GUI button authority: global BTN-180 owner recipe
User expressly rejected both Button 19 and the experimental flat-crystal 7-toolbar skin (PR #78). Toolbar-actions (7), left-rail (8), utility-row, channel-actions, and header Audio Device use ONE shared `btn-180` CSS skin from `web/styles-src/20-button-180.css`.
- OFF `#b3b3b3`, border `var(--b180-edge)`, ink `var(--b180-off-ink)`.
- Seven-layer common rest shadow starts `inset 5px 5px 2px -1px rgba(0,0,0,.30)` and second `inset 0 0 1px 1px rgba(255,255,255,.14)`; exact 7 entries in the owner CSS.
- While physically `:active`, move `translateY(1px)`; shadow starts `inset 3px 2px 2px -1px rgba(0,0,0,.50)`, second `inset 0 0 1px 0 rgba(255,255,255,.14)`, all seven layers including lower cast (no latched physical depth).
- ON utility-row cyan soft, ON left-rail magenta soft, toolbar selected pale silver-white. Functional channel MUTE/SOLO coral/amber, MASTER stereo blue/mono red with black mono ink remain independent semantic states.
- No Button 19 `fx-nav` hooks. `21-navigation-fx-power.css` remains an empty compatibility module for deterministic build order. `40-rail-pan.css` owns geometry; `20-button-180.css` owns the button skin. Preserve PAN 13/15/12, stereo VU, 36×77 faders, native backend.
- `scripts/build_ui_css.py --check`, `tests/button_asset_test.py`, Chromium/Firefox UI, Windows native CI must pass before merging. This block supersedes all older 2026-10-26 Button 19/flat crystal instructions.

## 2026-10-27 — BTN-180 perceptual palette and native device status
The owner accepted the shared seven-shadow BTN-180 physical pad but asked for a *balanced* lit palette (red MUTE, yellow SOLO, blue STEREO slightly brighter) and Audio Device to match the seven toolbar keys. ONLY semantic palette changes are allowed: preserve `inset 5px 5px 2px -1px rgba(0,0,0,.30)`, 7 resting shadows, 7 `:active` shadows, 1px press, 3px radius, and all existing layout/FX/PAN/fader/meter/audio-DSP mechanics. Single skin owner: `web/styles-src/20-button-180.css`.
Palette owner tokens: cyan `#83d6e0`, green/device-online `#79dd9e`, magenta/rail `#e3a6d8`, purple `#c4a9e8`, coral MUTE `#f38895`, amber SOLO `#edc669`, blue STEREO `#77baeb`. Keep dark captions; minimum per-token text contrast 6:1 and lit-role OKLab lightness spread below 0.11 verified by `tests/button_asset_test.py`. Master stereo/mono in `42-mixer-premium.css` use these tokens, preserving MONO black ink. `#headerDriver` inherits BTN-180 ink rather than the old cyan header style.
Audio Device is **OFF/grey in web-only preview**. Native `arMixStatus` callbacks in both `web/bridge.js` and `web/native-bridge.js` set its `.on` class and `data-audio-connected` only when `connected && !error`, never just because setup is opened. In Chromium screenshots, `.on` is temporarily simulated solely to inspect green; no preview connection is falsely advertised. Use normal CI, source/offline parity, browser smoke and Windows native gate. This section supersedes earlier color codes while keeping the approved Button-180 geometry unchanged.
