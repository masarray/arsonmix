# ArSonMix Routing R0/R1 — focused next-thread handoff (2026-10-29)

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

