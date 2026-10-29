# ArSonMix — Canonical Progress Reconciliation & Next Execution (2026-10-29 17:55 WIB)

> **SUPERSEDED FOR NEW UX/IO ROADMAP (2026-10-30):** Use [PRD v2.0](PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md), [ARD v2.0](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md) and [new handoff](EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md). This file is the **2026-10-29 historical source verification and issue ledger**, not permission to reconstruct the old ROUTING-vs-FX overlay. Current base has 10CH+4AUX, CH1 actual multi-VST3, 1 routed VST3 per other lane, MAIN L/R physical output read-only; custom I/O and bus workflow are next. #15 hardware FAT is open. Old dates and PR-number plans below must be rechecked against current main before use.


> **LATEST ROUTING UX 2026-10-30:** eMotion-informed but original ArSonMix console patchbay visual unification R3 [PR #164](https://github.com/masarray/arsonmix/pull/164) MERGED `71ab580`. Shared BTN-180 material on all routing operator controls, dense real 10CH INPUT matrix, 10×4 PRE/POST AUX, less noisy native session identity and explicit OUTPUT MUTE state. Proven exact-head Chromium 1440/1680 screenshots and native ACK workflow in [40956088106](https://github.com/masarray/arsonmix/actions/runs/40956088106) SUCCESS; main Windows compile pending and real hardware FAT #15 open. Do not reintroduce old flat beige routing buttons, stretched 159px input columns or CH NONE clipping. Source milestone design: `docs/R3_ROUTING_PATCHBAY_BTN180_VISUAL_SYSTEM_20261030.md`.


> **POST-#163 VERIFIED MERGES 2026-10-29:** R0B B3 #160 MERGED `3af8470`; Windows main distributable [40904834549](https://github.com/masarray/arsonmix/actions/runs/40904834549) SUCCESS (artifact had obsolete provenance text despite real B3 source; corrected in later repo). P4 Show worker-browse and DRY-only native revisioned recall #163 MERGED `5667eec`, exact-head Windows [40905958127](https://github.com/masarray/arsonmix/actions/runs/40905958127) and Chromium [40905958087](https://github.com/masarray/arsonmix/actions/runs/40905958087) SUCCESS. Main `fb0193e` fixes package manifest/nativeDSP to native 10CH+4AUX without falsely claiming hardware qualification; final main artifact CI pending. #15 real physical crackle FAT still open, no full vendor VST Show recall/PDC/physical output patch. Supersedes any 'B3 / P4 next' dated snapshots below.


> **VERIFIED LATEST `main` MERGES — 2026-10-29 post-#159:** R0B B3 [#157](https://github.com/masarray/arsonmix/pull/157) merged as `a9afddc` (Windows native 50/50 CTest/package head check [40890927975](https://github.com/masarray/arsonmix/actions/runs/40890927975) SUCCESS). NORMAL DIRECT output is fail-silent when native routing unpatched/invalid/stopped; FX graph handoff ACK/retirement still progresses. R2 [#159](https://github.com/masarray/arsonmix/pull/159) merged as `2acb509` (Chromium [40897545528](https://github.com/masarray/arsonmix/actions/runs/40897545528) SUCCESS): compact native CH→4 AUX send matrix, PRE/POST+gain/pan inspector, CH→CH advanced sends, duplicate/cycle preflight, source-of-truth DRAFT/ACK. Neither change is a hardware crackle certification; #15 remains OPEN and #149 remains OPEN for final full selected-device product acceptance and legacy alias cleanup. Older dated claims that B3 or R2 picker/matrix are not yet implemented are superseded.


> **LATEST SOURCE/CI RECONCILIATION — 2026-10-29:** R0B B2 [#156](https://github.com/masarray/arsonmix/pull/156) MERGED on `main@e62d377`: CH1 native FX ADD/REMOVE now stage prepared graph snapshots rather than detach/reopen the audio callback. Single routed 10CH+4AUX pass performs de-clicked buffer-boundary publication; stable VST3 identity, Orbit rebinding, control-owned one-time `releaseResources`, failure rollback and deferred positive UI ACK. **50/50 Windows native CTests + executable/package verification PASS** on exact PR head `0c53f9a` ([run 40871937375](https://github.com/masarray/arsonmix/actions/runs/40871937375)); includes 2,000 scripted ADD/REMOVE ownership handoffs. Gate A #152 and B1 #153 were already merged. **R0B #149 not yet complete**: B3 native selected-device always-on muted session / no LAB-STOP split, hotplug rollback, real hardware #15 FAT remain. Older #147 open claims are archival, not current work.


> **POST-AUDIT UPDATE (2026-10-29):** Gate A [#152](https://github.com/masarray/arsonmix/pull/152) **MERGED** (`72a4bfc`); old #147/#151 **CLOSED/superseded**. R0B B1 [#153](https://github.com/masarray/arsonmix/pull/153) **MERGED** (`eb94203`): CH1 FX MOVE safely runs inside existing 10CH+4AUX callback with stable original VST3 instance/Orbit and atomic two-buffer de-click; exact-head Windows native/RT/ZIP [run 40865180442](https://github.com/masarray/arsonmix/actions/runs/40865180442) SUCCESS. **#149 still OPEN** for unified muted startup, live ADD/REMOVE/shared ownership and device rollback. R2/R3 and physical 0-FX crackle #15 FAT still OPEN. Older `#147 OPEN` and G0 pending instructions below are a superseded 17:55 WIB snapshot, NOT next actions.


> **ROUTING-FOCUSED EXECUTION HANDOFF:** [docs/HANDOFF_ARSONMIX_ROUTING_R0_NEXT_THREAD_20261029.md](HANDOFF_ARSONMIX_ROUTING_R0_NEXT_THREAD_20261029.md) provides the exact R0/R1 done inventory, #147 overlap/negative-ACK merge gate, R0B #149, R2 INTERNAL/AUX UI, R3 native FAT and anti-regression checklist. Read it for Routing coding; this canonical document remains authoritative for all-project progress.


> **START HERE for cross-thread continuation.** This document is a verified status snapshot from GitHub. It supersedes *status claims* in older dated handoffs, README, AGENTS historical appendices and old issue descriptions, not their engineering evidence. Re-read live main/PRs/CI before code changes. No new feature or physical FAT is claimed by this documentation.

## 0. Verified frozen baseline

- Repository: https://github.com/masarray/arsonmix
- Main commit at audit: **4d14251bc21ed755e08a51b50aa3f1bcd2008cb2** (2026-10-29 10:01:14 UTC).
- Main Windows native build/CTest/package: **SUCCESS**, https://github.com/masarray/arsonmix/actions/runs/40854197486
- Main Chromium UI smoke: **SUCCESS**, https://github.com/masarray/arsonmix/actions/runs/40854197550
- Main Pages publish: **SUCCESS**, https://github.com/masarray/arsonmix/actions/runs/40854346099
- **None of the above is a certified physical ASIO/WASAPI clean-audio FAT.** Crackle issue #15 remains OPEN and must not be marked fixed based only on CI.

### Status contract

- **MERGED** = code in main and relevant automatic checks observed, not hardware-certified.
- **PARTIAL** = an important genuine working slice exists, but product acceptance not complete.
- **OPEN** = active issue/PR not merged, regardless of branch CI status.
- **FAT UNQUALIFIED** = no adequate real-hardware evidence of the quality claim.
- The current target is **ONE production live mixer**, not selectable Legacy CH1 vs operator Routing LAB.

## 1. Already delivered — DO NOT REBUILD

| Area | Actual status at verified main | Protected source / evidence |
| --- | --- | --- |
| Approved compact 10CH+MASTER skin; no heavy passive hover | MERGED | #140 universal LCD/splash, #145 hover-inert, main GUI CSS build parity |
| Native 10 input strips + 4 independent stereo AUX FX returns | MERGED but opt-in routing path | P1I/P1J/P1K, docs/P1K_TEN_INPUTS_FOUR_DEDICATED_AUX_20261029.md |
| Stable per-lane VST3 instance reuse across eligible route modifications | MERGED partial | P1J, LiveChannelFxGraph |
| CH1 legacy multi-FX and Orbit executes *inside* 10CH route graph | R0A MERGED, #144 | docs/R0_UNIFIED_CH1_LANE_MILESTONE_20261029.md |
| Real native input patch grid, sparse physical ports, stereo L/R, device epoch/ACK | R1 MERGED, #146 | docs/R1_NATIVE_INPUT_PATCH_MATRIX_20261029.md |
| Premium integrated ROUTING workspace rather than separate center dialog | MERGED in 4d14251 | docs/R0_PREMIUM_ROUTING_WORKSPACE_20261029.md |
| FX rack atomic MOVE, stable instance identity, bypass envelope, DearVR POLAR Orbit | MERGED foundations | #122, #107 and Orbit milestones; protect from old branches |
| MASTER native LUFS-I + 4x dBTP analysis | MERGED only for 48k DIRECT stereo | #142, docs/P1K_NATIVE_BS1770_DBTp_20261029.md |
| Portable Studio Plugin Vault and verified backup foundation | PARTIAL | #85, docs/ARD_PORTABLE_STUDIO_V1_20261027.md |
| Show/Snapshot schema, transactional storage, native zero-VST DRY Show SAVE | MERGED partial | P4 stages 1–3; not full FX-equipped live recall |
| Binary packaging/provenance independent verifier, lean Windows+Chromium CI | MERGED foundations | docs/P0_14_RELEASE_ENGINE_WORKFLOW_AUDIT_20261027.md; docs/CI_WORKFLOW_STRATEGY_20261028.md |

Do not duplicate a routing renderer, second audio callback, second FX bus graph, second snapshot authority or visual patchbay. Retain approved metal assets and CSS ownership; source-generated CSS/standalone parity is mandatory.

## 2. True remaining gaps, not cosmetic unfinished labels

### P0: native production graph consolidation — issue #149 (R0B)
- **OPEN / NOT delivered**: startup still has legacy CH1 vs opt-in LiveDryRoutingLab lifecycle; R0A does not equal full single production mixer.
- CH2–10 must not be presented as always-live if their native topology is not active/valid. CH1 FX ADD/REMOVE/MOVE/Orbit lifecycle has not been fully migrated to a continuous unified graph.
- Deliver one selected-device render graph, preserve processor identity/state/tail, ACK and mute/epoch fences, rollback, and output silence on missing ports.
- No UI rename-only shortcut. Issue: https://github.com/masarray/arsonmix/issues/149

### P0: real device/zero-FX reliability — issue #15
- **OPEN / FAT UNQUALIFIED**: physical zero-VST crackle was observed. Different capture/render clocks may contribute but are not proven sole cause. Use output-only, capture-only, dry-through and RT budget/provenance tests. Do not equate callback gaps with hardware render underruns.
- No physical acceptance for uninterrupted live changes, click-free FX moves, ASIO/WASAPI rates/devices just because Windows CI passes.
- Issue: https://github.com/masarray/arsonmix/issues/15

### P0: transactional negative ACK / pending state — PR #147
- **OPEN** on head 904c40ce828dc7832fe6ad53f58fc9143be4a01f. Targets Studio backup busy rejecting routingDryApply before the standard handler without a negative ACK, leaving UI APPLY pending.
- Branch Chromium SUCCESS https://github.com/masarray/arsonmix/actions/runs/40850266409 and Windows SUCCESS https://github.com/masarray/arsonmix/actions/runs/40850266555 . These are on PR head, NOT latest main.
- Reconcile tiny diff (Main.cpp, web/monitor.js, UI/tests), update base if required, rerun required exact-head validation and merge as an early corrective milestone. Avoid redundant reimplementation.

### P1: full native Show recall and safe UI persistence
- PR #141 **OPEN**, DRY-only Show browse/recall, zero FX. It is based on older code and uses legacy LAB UX. Salvage functional native validation/store/ACK, adapt to R0B and merged R1 workspace; do **not** bulk merge old Main.cpp / monitor.js / bridge.js.
- Issue #132 **OPEN**: browser UI-only snapshot mutation can be non-atomic on storage failure/malformed recall. Keep UI preview state separate from native truth until native SSOT is ready. One durable Show authority, no misleading real-VST claim.
- Full VST3 state capture/recall including plugin identity/Orbit, hardware remapping, snapshot safes, safe FX lifecycle and portable restore remain **NOT delivered**.
- PR #128 **OPEN stale**; its useful concept/code should be reviewed selectively; portions already merged/superseded.

### P1: internal FX/send and physical outputs
- Existing 4 dedicated AUX stereo **return to MAIN**; they are not four arbitrary hardware outputs. Current OUTPUT PATCH tab is **read only** MAIN L/R. Multi-bus physical output matrix not implemented.
- R2: consolidate internal send matrix/FX returns, mix vs FX bus semantics; preserve PRE/POST, no cycle/self-feedback and stable FX state. Do not create redundant AUX engine.
- R3: device-bound output patch for supported MAIN/monitor/record/sub/headphone ports, including mono/stereo and driver-masked sparse ports. Reject missing/stale/clock-incompatible endpoints; no UI fake functionality.
- Group/DCA/MATRIX/CUE and arbitrary bus-to-bus routes are **not automatically delivered** by 4 AUX.

### P1/P2: PDC, multitrack and platforms
- Plugin Delay Compensation and phase-aligned parallel dry/wet, seamless live topology edits, all-channel multi-insert VST3, master inserts and complete hardware output modes require dedicated gates.
- Native master LUFS-I / true peak currently qualified for **48k DIRECT** path only; 44.1/96k and independent clock are not silently supported. Issue #135 remains open for broader acceptance although #142 merged the 48k slice.
- Windows-first JUCE VST3. Mobile remote controller != Android native VST3 host; neither is yet a production deliverable. System audio capture, mix-minus, multitrack playback are later features.

## 3. Open PR triage — order, ownership and stale branches

| PR(s) | Disposition | Why |
| --- | --- | --- |
| #147 | PRIORITY review/rebase/merge | Small safety/UX pending-ACK defect, branch checks green |
| #141 | SALVAGE and reconcile after R0B contract | DRY Show recall, old LAB/GUI and older main base |
| #128 | DO NOT BULK MERGE | GUI/Audio Setup/snapshots overlap already merged main |
| #115, #117 | HISTORICAL/SUPERSEDED for production graph | Early dry routing code predates P1I/P1K; extract only unmerged tested behavior |
| #113, #59, #60 | HISTORICAL/DIAGNOSTIC | Old handoffs/GUI experiments, not source of current approved skin |
| #25, #26, #29 and #1–10 | HISTORICAL/EXPERIMENTAL | Do not replace merged P0 native device, FX or current GUI from old stacked heads |

**Do not close an old PR just to clean counters before checking unique functionality and owner instructions.** Mark it superseded with pointer/patch inventory if actually safe. Archive conflicts are not implementation evidence.

## 4. Issue reconciliation — avoid misleading task status

- Issue #143 is **CLOSED**, but full R0B was not delivered. NEW active issue #149 explicitly covers this gap. Do not reopen/rewrite #143's historical product request blindly.
- Issue #135 is OPEN although 48k direct MASTER measurement (#142) is already merged; preserve it for wider sample-rate/independent clock acceptance only, no second 48k analyzer.
- Issue #109 describes many capabilities as absent at its 2026-10-28 baseline; its IN PATCH/10CH/AUX requirements are partially implemented. Track remaining physical output/GROUP/graph capabilities without resetting the original graph.
- Issue #95 contains older all-CH rack and visual requirements; all-channel VST3 chain lifecycle remains incomplete while some drag/reorder/visual items are merged. Do not rebuild approved faders/hover/Orbit.
- Issue #85 continues to control unlimited **catalog import** vs bounded simultaneous realtime DSP; identical public/personal binary capabilities, no proprietary VST in public package, portable workspace, explicit device revalidation.
- Issue #137 is a **historical handoff snapshot** (older main and transient CI failure) and must not override this refreshed live audit.

## 5. Updated work sequence: dependencies, acceptance and CI economy

1. **G0: merge/resolve #147 ACK correctness**, after validating against latest main. Deliver negative ACK on busy/rejection, UI pending always releases and acceptedRoute remains unchanged. No Audio callback changes.
2. **G1: R0B #149 single production mixer**, end legacy CH1 opt-in LAB split; preserve native CH1 multi-insert, Orbit and 10CH+4AUX real routing. Explicit unit/control-graph tests; truthful startup; fail-silent hardware/device transitions.
3. **G2: R2 INTERNAL/send/FX refinement**, use existing 4 AUX foundation and unified graph; define FX-return vs mix-bus behavior rather than copy/paste new buses.
4. **G3: R3 actual device-bound OUTPUT PATCH**, enabled physical output ports, independent bus routing, sparse stereo/mono, gain/summing/headroom and per-output safety.
5. **G4: PDC + safe live structural FX edits**, preserved plugin state/tail, latency report and phase alignment; vendor/driver constraints explicit.
6. **G5: FULL Shows/Snapshots + Portable Studio**, on real native SSOT; transactional state capture and verified recovery, device remap and no auto-unmute.
7. **G6: playback/system audio/mix-minus/remote/mobile platform choices** only after native live core has acceptance evidence.

**Parallel lane:** issue #15 root-cause isolation and consolidated real-device hardware FAT. This is a release blocker, not a reason to revert useful already-merged work. Document the exact hardware, backend, clock, sample rate, buffer, plugin set, measured RT budget, provenance-tagged discontinuities and loopback latency before certification.

### Required merge gates

- Before implementation: refresh main SHA, open PRs, owned paths and build/test baseline. Never assume a milestone is open from older document.
- One subsystem owner per patch: realtime native C++ core, device bridge, native Show/Studio, web source/UI, generated artifacts. No competing threads overwrite identical source files without a deliberate dependency/rebase.
- Never allocate/lock/log/scan/serialize/perform file I/O or instantiate/release plugins inside the callback. Control prepares, validates, publishes atomically at buffer boundary, keeps bounded graph retirement.
- Structural routes require exact native physical mask, epoch/revision, expected current topology, fail-silent/OUTPUT MUTE gates where appropriate, and explicit accept/reject ACK. UI DRAFT != LIVE.
- Exact-head native MSVC+CTest and Chromium as relevant; dedicated hardware FAT separately; generated stylesheet and embedded preview byte parity when GUI changes.
- Keep CI efficient: one Windows workflow for real binary + native tests, one Chromium smoke for GUI, no redundant build matrices or test relaxations. Doc-only changes should not consume costly CI.
- Before claiming milestone done, point to changed implementation, passing automated tests on that exact SHA, user-visible native behavior and separate physical FAT qualification (or state explicitly that FAT is pending).

## 6. Customer voice translated into engineering priorities

Based on 85 primary comments + 47 replies retrieved for https://youtu.be/mFzR9xBzuao on 2026-10-29, the core signals are: configurable ASIO I/O and multi-output, true routing matrix, shared FX send/return (CPU/latency concern), AUX/group/monitor live workflow, stable distributable download, plus requests for multitrack, system-audio loopback, Android/macOS and vocal harmony. This is audience input, not proof of demand ranking across the global market.

**Product decision:** one Windows-first, cross-platform-ready, modular native mixer with 10CH+4AUX current baseline; capability-aware expandability, no proprietary device assumption, small responsive GUI, honest output availability, Shared FX and Portable Studio for everyone. Do not create separate Lab, premium/private engine or feature-limited public app.

## 7. Cross-thread handoff checklist

- Verify latest main/head and recent CI; use this doc as starting inventory, not permanent SHA pin.
- Read AGENTS.md, docs/GUI_BASELINE_LOCK_2026-10-25.md, docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md, docs/R1_NATIVE_INPUT_PATCH_MATRIX_20261029.md, docs/R0_PREMIUM_ROUTING_WORKSPACE_20261029.md, docs/P1J_STABLE_FX_ROUTING_SESSION_20261029.md, docs/P1K_TEN_INPUTS_FOUR_DEDICATED_AUX_20261029.md and issues #149/#15/#85.
- Make one meaningful native milestone per PR; cite the exact current base, test evidence, capability claim and remaining release gates. Do not make docs into endless planning work.
- When production status materially changes, update this canonical status or link a newer reconciliation in AGENTS.md; don't leave contradictory old status as the only entrypoint.
