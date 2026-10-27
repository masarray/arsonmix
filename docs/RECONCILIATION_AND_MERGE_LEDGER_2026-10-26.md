# ArSonMix — global PR, GUI & native merge reconciliation

**Snapshot:** 2026-10-26 (after PR #57 merged; verify latest SHA before use). **Repository:** `masarray/arsonmix`. **Audited latest production/development head:** `main@8d7ccab5522dae2f813ca13fc01e700fc1e1a9fb` (PR #57 GUI merge safeguards, following PR #55 ASIO route fence). **Re-read GitHub state before any write/merge; this SHA is a snapshot, not a permanent "latest."**

## 1. Actual latest baseline and evidence

- Last fully verified all-3 green main before PR #57: `8d5f974` — Chromium PASS [40267615450](https://github.com/masarray/arsonmix/actions/runs/40267615450), Windows PASS [40267615334](https://github.com/masarray/arsonmix/actions/runs/40267615334), Pages PASS [40267689657](https://github.com/masarray/arsonmix/actions/runs/40267689657).
- Latest `8d7ccab` after [#57](https://github.com/masarray/arsonmix/pull/57): Chromium PASS [40280837302](https://github.com/masarray/arsonmix/actions/runs/40280837302), Pages PASS [40280916936](https://github.com/masarray/arsonmix/actions/runs/40280916936); Windows native [40280837306](https://github.com/masarray/arsonmix/actions/runs/40280837306) **still running at reconciliation**, so do not claim full green before result.
- [#57](https://github.com/masarray/arsonmix/pull/57) also merged canonical **GUI-specific reconciliation** into `docs/GUI_MERGE_RECONCILIATION_2026-10-26.md`; this ledger complements it with *global native PR and release* disposition, and does not supersede its visual acceptance authority.
- These automated green checks establish source/build/UI test acceptance only. **They do NOT certify clean audio/physical xrun-free ASIO/WASAPI operation**, real round-trip latency, plugin-vendor stability, or Firefox pointer smoothness.

### Current GUI truth — critical supersession

- [#51](https://github.com/masarray/arsonmix/pull/51) merged the **FX-power-like** look for toolbar-actions and left rail.
- **Later [#53](https://github.com/masarray/arsonmix/pull/53) superseded that look**: production navigation is now **Button 19**, gray raised OFF (`#e0e4e8`), green inset latched ON (`#39ef72`), with transient press only for one-shot controls.
- Current owner remains `web/styles-src/21-navigation-fx-power.css` **for historical build-order compatibility**, but the file now contains **Button 19**, not FX Power styling. Do NOT restore #51 or the independently closed/unmerged #49 wholesale.
- Mixer channel/master controls retain the independently owned **Button 180** original neutral OFF palette (`#858283` face/`#636363` edge/`#2b2b2b` ink), ON semantic colors, and transient 1px physical press. Original rack `.fx-power` remains dark-blue/black FX glass.
- [#54](https://github.com/masarray/arsonmix/pull/54) improved preview compositor/meter/drag behavior, but [issue #56](https://github.com/masarray/arsonmix/issues/56) remains OPEN. **New [PR #58](https://github.com/masarray/arsonmix/pull/58)** contains lighter hover and a six-mode Firefox A/B profiling lab; at this snapshot it is OPEN and UNMERGED. It is a measured-field-validation candidate, not proof that physical Windows Firefox stutter is fixed.

### Current native-engine truth

- [#13](https://github.com/masarray/arsonmix/pull/13) integrated the former P1/P2.5 native feature lineage onto approved GUI without stale-branch overwrite; [#14](https://github.com/masarray/arsonmix/pull/14) added safety follow-up.
- [#35](https://github.com/masarray/arsonmix/pull/35) integrated the real independent JUCE WASAPI capture/render adapters, bounded FIFO/ASRC and DIRECT fallback, as an experimental opt-in path. Subsequent [#39](https://github.com/masarray/arsonmix/pull/39), [#40](https://github.com/masarray/arsonmix/pull/40), [#42](https://github.com/masarray/arsonmix/pull/42), [#45](https://github.com/masarray/arsonmix/pull/45), [#50](https://github.com/masarray/arsonmix/pull/50), [#52](https://github.com/masarray/arsonmix/pull/52), and [#55](https://github.com/masarray/arsonmix/pull/55) harden continuity, endpoint identity, ASIO priority, source-labeled diagnostics and fail-closed DIRECT authorization.
- [Issue #15](https://github.com/masarray/arsonmix/issues/15) remains **P0 release-blocking**. The user's severe 0-VST crackle report has not been disproved by validated hardware re-tests. ASIO is the live-performance priority; do not infer a guaranteed low-latency or clean-output result from CI.

## 2. Open PR inventory and merge disposition

At snapshot: **12 historical open PRs** (9 stacked P1/P2 + 3 older P0) **plus active Firefox performance PR #58** and this docs reconciliation PR #59. This is 14 total open at audit. **No historical PR is approved for direct merge into latest main. PR #58 is an independent new performance candidate and must pass current CI; Firefox user testing is a distinct acceptance stage.** The old P1/P2 tree is far behind main and its app/UI/CMake state would regress integration; the old P0 branches are ~95–96 commits behind with conflicting `LiveAudioEngine.h`/device-adapter ownership.

| PRs | State | Reconciliation / next action |
| --- | --- | --- |
| [#1](https://github.com/masarray/arsonmix/pull/1), [#3](https://github.com/masarray/arsonmix/pull/3), [#4](https://github.com/masarray/arsonmix/pull/4), [#5](https://github.com/masarray/arsonmix/pull/5), [#6](https://github.com/masarray/arsonmix/pull/6), [#7](https://github.com/masarray/arsonmix/pull/7), [#8](https://github.com/masarray/arsonmix/pull/8), [#9](https://github.com/masarray/arsonmix/pull/9), [#10](https://github.com/masarray/arsonmix/pull/10) | **9 open Draft archival** | Superseded as an integration line by #13/#14. Do not merge the ancestral stacked branches, change their bases, or close their provenance during current P0 qualification. Isolate a specific missing capability/test on a NEW `main` branch when evidence justifies it. |
| [#25](https://github.com/masarray/arsonmix/pull/25) | **Draft (quarantined 2026-10-26)** | Older combined dual-clock split adapter; real architecture now from #35 plus hardening. No whole-branch merge; leave comment and preserve source. |
| [#26](https://github.com/masarray/arsonmix/pull/26) | **Draft (quarantined 2026-10-26)** | Older generation recovery/telemetry prototype; current architecture uses `JuceIndependentClockDevice`, continuity watchdog and verified restore/route fences. No whole-branch merge. |
| [#29](https://github.com/masarray/arsonmix/pull/29) | **Draft (archival)** | Previous split-stream opt-in attempt; overlaps newer `main`. Only port a narrowly proven missing behavior as a NEW PR with current tests. |
| [#58](https://github.com/masarray/arsonmix/pull/58) | **Open active candidate** | Firefox pointer stutter A/B lab and lightweight hover; review against issue #56 and #57, require both CI gates before merge and preserve Button 19/materials. Physical Firefox profiling must still follow deployment. |

These are **not "all changes done" claims**. Historical branch references remain for inspectable evidence; missing physical tests and any unimplemented functionality must be tracked in issues, not resurrected by a destructive merge.

## 3. Four OPEN non-PR issues: what is genuinely not done

1. **[#15 — P0 physical crackle / WASAPI-ASIO acceptance](https://github.com/masarray/arsonmix/issues/15): OPEN.** Zero-FX hardware A/B, source-labeled output/capture discontinuity, latency and device-change validation required. One consolidated acceptance after engineering changes; no stable release claims.
2. **[#56 — Firefox pointer stutter](https://github.com/masarray/arsonmix/issues/56): OPEN.** Reproduce and profile on affected Windows Firefox; A/B animated hover, 25-Hz demo meters, `will-change` layers and preview iframe. Chromium PASS alone is insufficient.
3. **[#37 — CSS hardening](https://github.com/masarray/arsonmix/issues/37): OPEN.** Deterministic CSS module builder and Button 180/nav ownership completed in part; `10-console-legacy.css`, competing responsive breakpoints and CSS AST-driven reachability/specificity lint not fully migrated. Do not declare full CSS refactor finished.
4. **[#44 — Multi-theme GUI](https://github.com/masarray/arsonmix/issues/44): OPEN.** Approved Brushed Metal source/ARD/handoff exists, **runtime theme manager/selector is NOT implemented**; T0–T7 staged plan still pending. Keep current Button 19/mixer look unchanged until explicit visual approval.

## 4. Safe merge protocol (every next thread)

1. Re-read **latest** `main`, open PRs, issue #15, GUI lock, actual `21-navigation-fx-power.css`, CI runs and this ledger before edits.
2. For a PR based on an old version, compare ancestry and per-file diff against current `main`. Never cherry-pick an old `web/app.js`, `web/styles.css`, `src/LiveAudioEngine.h`, `src/Main.cpp` or CI wholesale.
3. Require coherent PR ownership and a **single testable milestone**, no unrelated GUI/audio changes. Native callback must have bounded, allocation/lock/I/O-free host logic; only claim verified measurements.
4. Run `python scripts/build_ui_css.py --check` for UI changes; HTML/JS/CSS offline/WebView2 parity, browser geometry, ON/OFF/tactile state, 12 source strips + stereo master, native Windows MSVC + CTest, and all relevant regression contracts.
5. Confirm latest PR head SHA is CI-green and GitHub mergeable; do not merge any failed/stale head. After merging, check `main` CI (Windows + Chromium) and GitHub Pages exact SHA. A publish badge is not a physical performance test.
6. Keep P0 #15 open until actual 0-VST and representative-VST **hardware** FAT passes; record test device/backend, actual native rates/periods and a full diagnostic report. Track native `DirectRouteFence` faults separately from host FIFO underruns and real hardware xruns.
7. Do not conflate historical GUI v0.8 **structural** lock with obsolete color instructions; current physical skin is the **later** Button 19 PR #53, while Button 180 still owns mixer channels/master.

## 5. Next meaningful milestone

**P0: physical clean dry audio qualification** is the release gate. The next native engineering PR should target a *demonstrated* gap from #15/#55 against latest `main` (truthful endpoint/render-starvation evidence, ASIO/direct fallback correctness, deterministic stress suite) and pass CI. Then do **one** consolidated hardware FAT, not manual test after each small PR. In parallel, treat Firefox #56 as a measured GUI performance task, not as a reason to blanket-remove filters or regenerate the visual design. CSS #37 and theme #44 are staged and independent.

**Merge decision at snapshot:** Already integrated #13/#35/#51/#53/#54/#55/**#57**. **No merge of old open PRs**; evaluate new PR #58 separately. #25/#26 were placed in Draft and #29 retained Draft; remaining nine P1/P2 drafts remain available as provenance. This ledger itself is documentation reconciliation, **not a new audio or GUI implementation**.
