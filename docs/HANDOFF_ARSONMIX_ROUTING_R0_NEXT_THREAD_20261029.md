# ArSonMix Routing R0/R1 — focused next-thread handoff (2026-10-29)

> **OLDER R0 HANDOFF — SUPERSEDED FOR GUI/IMPLEMENTATION PRIORITY (2026-10-30):** Follow [new G1/G2 reform handoff](EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md), [PRD](PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md) and [ARD](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md). Keep the previous route-engine safety tests, but replace routing-first page behavior with MIX FX/ROUTE/SENDS toggle, standalone FX Rack, native custom I/O setup, proper AUX buses, and eventual real output patch.


> **LATEST PRODUCTION HANDOFF 2026-10-29:** B3 native always-on unpatched verified 10CH+4AUX #160 MERGED; P4 native DRY Show SAVE→BROWSE→RECALL #163 MERGED as `5667eec`, exact-head Windows/CTest/package and Chromium PASS; release provenance fixed in `fb0193e` and final main Windows pack CI not yet confirmed. No duplicate FX/callback, P4 does not restore VST-equipped Shows or auto-unmute, device-bound physical port numbers require human validation. Existing hardware FAT #15 **OPEN**; no real-world crackle certification. Next release-relevant work: real-device 0-FX ASIO/WASAPI, output-only/capture-only/dry-through isolation, safe FX chain/Orbit, external GPU UI A/B, PDC and physical output patch. Stale B3/P4 'NEXT' lines below are HISTORICAL only. Concurrent plugin manager #161 is separate; preserve R2 internal send matrix and shipped GUI.


> **CURRENT P4 DEPENDENCY SNAPSHOT (2026-10-29):** #160 new B3 native selected-device SILENT 10CH+4AUX prepared startup is OPEN / exact-head CI pending. #157 fail-silent-only B3 and #159 AUX matrix are already merged. A new actual 12-file source branch `feat/p4-native-dry-show-browse-recall-modern-20261029` at `3902e0d` contains worker-backed verified native Show BROWSE and DRY-only RECALL plus MASTER mono/linear state and deterministic tests; **not merged, not CI green**. It is based on main `ce5f06b`, so **DO NOT blindly merge/rebase against #160**; reconcile Main.cpp, LiveAudioEngine.h, LiveDryRoutingLab.h, web/monitor.js and two test suites from fresh main after #160, preserving #160 unpatched session and R2 AUX matrix. Reference doc within staged branch: `docs/P4_NATIVE_DRY_SHOW_BROWSE_RECALL_20261029.md`. No full VST3 Show state or hardware FAT claim; #15 remains open.


> **CURRENT NEXT-THREAD ENTRYPOINT — post-#159, 2026-10-29:** R0B B3 [#157](https://github.com/masarray/arsonmix/pull/157) MERGED as `a9afddc`, exact-head Windows native/CTest/package [40890927975](https://github.com/masarray/arsonmix/actions/runs/40890927975) SUCCESS. NORMAL DIRECT callback no longer exposes legacy CH1 after unpatched/STOP/hotplug, and pending FX handoff still ACKs during silence. R2 compact **10×4 CH→AUX send matrix** with PRE/POST, level/pan inspector, CH→CH advanced editing and local DAG preflight [#159](https://github.com/masarray/arsonmix/pull/159) MERGED as `2acb509`, Chromium smoke [40897545528](https://github.com/masarray/arsonmix/actions/runs/40897545528) SUCCESS. **Do not redo G0/B1/B2/B3, native INPUT PATCH or R2 AUX matrix/picker.** Next meaningful work: verify #149 full native lifecycle/recovery and old LAB command semantics against exact current main; R3 real device ASIO/WASAPI zero-FX crackle FAT #15, multi-DPI/onboard GPU perf acceptance; native full FX-equipped Show recall, PDC, physical OUTPUT PATCH remain separately unqualified. All older 'B3 next' paragraphs are historical snapshots.


> **NEXT-THREAD R0B ENTRYPOINT — 2026-10-29:** R0/R1/R0A + Gate A #152 + B1 #153 + significant B2 [#156](https://github.com/masarray/arsonmix/pull/156) now MERGED; B2 adds prepared live CH1 FX ADD/REMOVE lifecycle with stable original VST3/Orbit instances, no routine callback detach and native ACK/de-click. Exact-head 50/50 Windows CTests and packaging PASS [run 40871937375](https://github.com/masarray/arsonmix/actions/runs/40871937375). **NEXT meaningful milestone B3:** one always-on native 10CH+4AUX production session on a verified selected device; unpatched output SILENT, no automatic input remap/unmute, atomic route revisions, device-loss rollback, replace visible LAB/STOP only when native path accepted. R2 INTERNAL/AUX and R3 real hardware remain; zero-FX crackle #15 not closed. Do not repeat G0/B1/B2 or revive old #147/#151 branches.


> **POST-HANDOFF UPDATE (2026-10-29):** G0 native Studio-busy negative ACK [#152](https://github.com/masarray/arsonmix/pull/152) and R0B B1 routed CH1 FX MOVE/de-click [#153](https://github.com/masarray/arsonmix/pull/153) are both MERGED. Stale #147/#151 are CLOSED/superseded. B1 preserved VST3/Orbit identity and used existing atomic phase ramp; exact-head Windows native/RT tests [40865180442](https://github.com/masarray/arsonmix/actions/runs/40865180442) PASS. **Next:** R0B #149 B2 prepared FX ADD/REMOVE shared lifetime, B3 one verified always-on muted 10CH+4AUX session with device-loss rollback. Then R2 INTERNAL/AUX UX and R3 hardware FAT; zero-FX #15 remains OPEN. Dated Gate A instructions below are retained as history, not repeat work.


> **Read this AFTER `AGENTS.md` and `docs/CANONICAL_PROGRESS_RECONCILIATION_20261029.md`.** This handoff narrows the next thread to routing GUI and unified production graph. The canonical reconciliation remains authoritative for the whole product. Never assume this saved main SHA is current; fetch GitHub first.

## 0. Verified state and links

- Repo: https://github.com/masarray/arsonmix
- Baseline at writing: `main@831ae2ebb94a83585a73b05bba8cbe7facea8110`; R0 premium routing workspace and R1 INPUT PATCH are ALREADY MERGED. The latest canonical reconciliation was committed at `831ae2e`; the handoff commit will advance main again.
- R0 workbench code: `web/monitor.js`, design/evidence: [R0 premium workspace](R0_PREMIUM_ROUTING_WORKSPACE_20261029.md). The new workspace shares the existing mixer toolbar, 156px rail and footer, not a dimmed pop-up. Static metal/graphite/teal CSS, no blur or hover repaint, fixed actions and scrollable middle.
- R1 functional matrix: [R1 input patch](R1_NATIVE_INPUT_PATCH_MATRIX_20261029.md). Native physical-port-mask columns, 10 CH rows, eight ports/page, bounded ~140px wide cells, MONO selection, explicit stereo L/R, sparse inputs, correct LIVE/DRAFT/REMOVED, native ACK/epoch/revision and REVERT. R1 [PR #146](https://github.com/masarray/arsonmix/pull/146) MERGED.
- R0A CH1 FX/Orbit native graph [PR #144](https://github.com/masarray/arsonmix/pull/144) MERGED; real CH1 FX Rack multi-inserts and dearVR Orbit coexist with 10 inputs + 4 dedicated AUX returns without instantiating a duplicate CH1 plugin. **This does not imply full single native production lifecycle (R0B).**
- Main baseline `4d14251` was green: Windows native/CTest [40854197486](https://github.com/masarray/arsonmix/actions/runs/40854197486), Chromium [40854197550](https://github.com/masarray/arsonmix/actions/runs/40854197550), GUI publish [40854346099](https://github.com/masarray/arsonmix/actions/runs/40854346099). Later doc-only commits change SHA; verify live CI rather than copying green labels.
- Open [PR #147](https://github.com/masarray/arsonmix/pull/147), head `904c40ce828dc7832fe6ad53f58fc9143be4a01f`: negative ACK on routing rejection during Studio backup. Its head Windows [40850266555](https://github.com/masarray/arsonmix/actions/runs/40850266555) and Chromium [40850266409](https://github.com/masarray/arsonmix/actions/runs/40850266409) PASS, **but it bases before merged R0 and overlaps `web/monitor.js`**. Reconcile rather than merge the old UI wholesale.
- Open [issue #149](https://github.com/masarray/arsonmix/issues/149): R0B one always-on native 10CH+4AUX mixer lifecycle, a genuine P0 unfinished product gate. Open [PR #141](https://github.com/masarray/arsonmix/pull/141): native DRY/zero-VST Show browse/recall, older branch, do not bulk merge. P0 hardware crackle [#15](https://github.com/masarray/arsonmix/issues/15) remains open.

## 1. Binding owner requirements — NO NEW LAB / NO NAIVE TRIAL-ERROR

- Build **one actual live ArSonMix product**, not separate production/experimental routing modes. Routing UI visually matches the approved main window: compact steel/graphite surfaces, LCD-black display, green ACTIVE, amber draft, clear muted/rejected status; no heavy typography, huge cards, noisy dropdown walls or redundant modes. Comparable professional live-mixer usability, original visual design.
- Owner prioritizes **light GPU pointer movement** above fancy hover. No CSS blur, backdrop-filter, brightness filter, animated shadow, hover-driven grid recalculation, continuous canvas, extra RAF/timers, massive catalog options eagerly mounted or full matrix redraw every 12Hz.
- Real hardware truth only: actual JUCE device mask/port labels, epoch, revision, OUTPUT MUTE gate; LIVE means `acceptedRoute` ACK—not merely clicked. Never silently remap on device change or unmute. No unsupported output patch or VST state shown as working.
- Protect current 10 CH + 4 dedicated AUX stereo returns, PRE/POST sends, P1J FX reuse/tails, FX MOVE/de-click, dearVR POLAR/Orbit and ownership/affinity. Callback must remain allocation-free/lock-free/I/O-free; no VST lifecycle, JSON, UI, COM or disk inside real-time callback.

## 2. Already implemented — do not duplicate

| Area | Reality |
| --- | --- |
| R0 premium integrated surface | DONE in main `web/monitor.js`, under existing toolbar, not an extra centered modal. Escape/nav closes and preserves audio draft semantics. |
| R1 INPUT PATCH | DONE: 10 CH, eight real ports/page, MONO/STEREO L/R, sparse inputs, cross-device invalidation, native ACK with revision and REVERT. |
| OUTPUT | Current MAIN L/R is deliberately **read-only**. Arbitrary CUE/independent output sends and hardware OUTPUT PATCH are future gates. |
| INTERNAL/SENDS | Existing real 10CH+4 AUX VST3/PRE/POST editor **works** but is still form-heavy. Lazy-loaded only when selected; not yet compact internal send matrix. |
| VST catalog | Legacy full `<select>` menus are built for up to 14 strips when INTERNAL tab opens. For large catalogs this multiplies DOM nodes; **one lazy searchable shared picker is NOT yet done.** |
| R0A engine | CH1 actual multi-insert/Orbit can process inside native route graph. Remaining default-vs-opt-in lifecycle still needs R0B; do not merely rename LAB to claim it is done. |
| Shows | Native bounded schema/staged persistence and DRY save foundations only; plugin-equipped full cross-PC scene recall is not delivered. |

## 3. Mandatory next-thread execution ladder

### Gate A — R1 ACK hardening (#147) FIRST
1. Fetch newest main, open PRs, CI. Diff #147 `src/Main.cpp`, `web/monitor.js`, `tests/ui_smoke.py`, `tests/live_routing_ui_contract_test.mjs` against merged R0. **Do not replace new Routing workspace DOM/CSS with #147's older monitor.js.**
2. An early `routingDryApply` rejection while Studio/Shows backup is busy must send exactly one **negative `arMixRoutingLabAck`**, unchanged authoritative accepted state, actionable error. UI must release pending/disabled APPLY and allow retry, without reporting LIVE.
3. Cover missing MUTE, stale epoch/revision, rapid double APPLY, no device, sparse port and duplicate stereo side, rejection during backup, STOP/unplug/reconnect and success/rejected ACK ordering.
4. One small scope PR/merge after exact-head targeted native tests + Chromium and Windows MSVC/CTest. No speculative CI reruns.

### Gate B — R0B unified always-on LIVE audio graph (#149)
1. Review actual default CH1 legacy path versus opt-in `LiveDryRoutingLab` after merged R0A. Produce explicit state machine, graph ownership/lease and device safety invariants before code. No duplicate callback, no guessing a physical input on startup, no auto-unmute.
2. Move supported FX add/remove/move/bypass/Orbit to **one native graph lifecycle** with prepared immutable graph, per-instance identity/state/tail, bounded publication/retirement, device epoch/revision and MUTE/ACK fences. Graph swaps must be callback-safe and fail silent/rollback.
3. Test default start, route APPLY, device change, plugin editor, repeated quick edits, no legacy CH1 double processing; preserve 10 inputs / 4 AUX PRE/POST and master. Scope into reviewable engine vertical slices.
4. Hardware #15 clean-audio FAT remains independent release blocker; CI never equals physical acceptance.

### Gate C — R2 INTERNAL/SENDS matrix + smart plugin picker
1. Replace current 14-strip dropdown-heavy INTERNAL area with compact send/return grid, contextual PRE/POST + gain inspector, AUX-to-MASTER and FX-return semantics. Preserve native directed acyclic/cycle checks and actual available targets.
2. Use **one on-demand searchable VST3 catalog picker** (bounded result count, escaped IDs, keyboard focus), not a full catalog replicated 14×. Event delegation and local cell updates; no hover animation or 12Hz full rerender.
3. Preserve DRAFT vs ACK LIVE, fail-closed structural edits requiring OUTPUT MUTE, PRE/POST gain and existing FX-instance reuse; do not invent GROUP/DCA/MATRIX/CUE or physical AUX outputs.

### Gate D — R3 professional UX + real hardware FAT
- Validate embedded WebView2 at 1440×820, 1593×928, 1680×920 and Windows DPI 100/125/150/175%; compare chrome, controls, min widths, scroll/sticky header/footer, keyboard focus and accessible labels.
- Measure first-open routing latency, DOM option count and p95 pointer-to-paint on owner's **standard/onboard-GPU** Windows PC; compare same device/plugin setup and baseline. Do not invent FPS gains.
- Confirm real ASIO/WASAPI routing, CH1 FX/Orbit and four AUX sends during performance with output/MUTE safety, no pops or false ACK, with exact backend/clock/rate/buffer and source-labeled fault telemetry. Keep #15 open absent proof.
- Actual hardware OUTPUT patch requires separate R3 backend implementation and explicit device-bound preflight; until then read-only UI is honest.

## 4. Ownership, tests and CI budget

| Owner | Files / rules |
| --- | --- |
| Routing GUI | `web/monitor.js` only: source-owned native styling, patch cells, lazy tabs, routing draft, ACK. Avoid copy-pasting from stale PRs. |
| Main window/bridge | `web/app.js`, `web/bridge.js`, `web/native-bridge.js`: approved 10CH+MASTER chrome/native status; don't overwrite with preview. |
| Native safety | `src/Main.cpp`, `src/LiveAudioEngine.h`, `src/LiveDryRoutingLab.h`, `src/LiveGraphHandoff.h`, `engine/core/{LiveChannelFxGraph,RealtimeRoutePublisher,NativeDryRouteCommand,PreparedMixerBlock,AuxBusPlan}.h`. Native command preflight vs RT callback strictly separated. |
| Tests | `tests/live_routing_ui_contract_test.mjs` and `tests/ui_smoke.py` cover R0 workbench, R1 patch, ACK, device swaps, output read-only; native CTest for graph. Add regressions rather than weaken tests. |
| Source docs | `docs/R0_PREMIUM_ROUTING_WORKSPACE_20261029.md`, `docs/R1_NATIVE_INPUT_PATCH_MATRIX_20261029.md`, `docs/P1J_STABLE_FX_ROUTING_SESSION_20261029.md`, `docs/GUI_BASELINE_LOCK_2026-10-25.md`, `docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md`. Historical v0.8 12+1 / hover guidance is superseded by current 10CH+MASTER, hover-inert source. |

Execution discipline: inspect latest SHA/PR overlap first; one reviewable gate per branch; local `node --check web/monitor.js`, `node --test tests/live_routing_ui_contract_test.mjs`, UI smoke and CSS parity **before** meaningful CI push. On GUI CSS ownership changes use `python scripts/build_ui_css.py --check`. Native changes require targeted CTest and one canonical Windows+Chromium exact-head workflow, log diagnosis instead of blind reruns. Confirm real hardware separately and record limitations. No extra CI matrix.

## 5. Direct prompt for next thread

> Use GitHub `masarray/arsonmix`. Read `AGENTS.md`, `docs/CANONICAL_PROGRESS_RECONCILIATION_20261029.md` and **`docs/HANDOFF_ARSONMIX_ROUTING_R0_NEXT_THREAD_20261029.md`**. Re-fetch latest main, open PRs and exact-head CI. **R0 integrated workspace, R1 INPUT PATCH and R0A CH1 FX/Orbit are already merged: DO NOT REDO THEM.** First reconcile open PR #147 rejected-ACK patch with merged R0 workspace, then implement R0B one always-on native 10CH+4AUX mixer (#149) in small safe engine changes, R2 compact INTERNAL/AUX SEND matrix plus one lazy VST3 picker, R3 native visual/performance and real ASIO/WASAPI FAT. Honor low-GPU no-hover styling, MUTE/epoch/revision/ACK and realtime guarantees; don't waste CI or overwrite other threads. Update evidence and this handoff after each milestone.

