# ArSonMix — Engineering Handoff / Next Thread (2026-10-28)

**Status: NOT FINISHED.** This is the canonical handoff for the work in the 2026-10-27/28 FX Rack, realtime-safe FX reorder, and real 10-channel audio routing thread. A successor should **not** assume all FX channels already work or that crackling is resolved. User approved an optimized, reliable, lightweight, flexible, maintainable, truly realtime engine — no naive trial-and-error, no fake UI/audio, no blindly merging stale branches.

Repo: https://github.com/masarray/arsonmix

## 0. Baseline and ownership: read before touching code

- Captured `main` SHA: **`c0d7c794a27fcdc7adbe77c6166a2e97b848b82c`**. This is **only a snapshot**; read current `main`, all relevant PRs, and source SHA at start of the next thread.
- Current main already includes **PR #105** capture-triage/DearVR POLAR integration, **PR #106** performant meter fast fall/ease-out, and **PR #107** sample-accurate FX bypass de-click. Preserve these. There may be new commits after this handoff.
- **#15** https://github.com/masarray/arsonmix/issues/15 — **OPEN, mandatory hardware zero-VST3 crackle gate**. Earlier Windows shared audio + independent capture/output hardware showed audible crackles with ZERO VST3 despite apparently fast DSP callback; callback gap != proven xrun. Must not claim bug fixed based on CI.
- **#95** https://github.com/masarray/arsonmix/issues/95 — owner requirements for all-channel FX Rack + actual source mapping + safe reorder + visuals.
- **PR #108** https://github.com/masarray/arsonmix/pull/108 — pure 10-strip/Master routing core (this thread's most recent NEW implementation). Head `465f5f78fd8228d248e9b1eb37a5f25e7610ba14`; at handoff **OPEN, not merged, mergeable_state=dirty**, behind main by three commits. CI **both PASS**, as below.
- **PR #102** https://github.com/masarray/arsonmix/pull/102 — GUI FX popup, true DAW insert ghost drag, Orbit marker/context Delete FX, fader improvements, atomic FX reorder and block fade. Head `5c8fb32dc79825caf845cea96178daa690710837`; at handoff **OPEN DRAFT, mergeable_state=dirty**, behind main by ten commits. **Do not merge wholesale** or overwrite PR #105/#106/#107 changes.
- **PR #97** https://github.com/masarray/arsonmix/pull/97 — overlapping older P0 FX GUI. Treat as reference only; reconcile one component owner.
- **PR #101** https://github.com/masarray/arsonmix/pull/101 — CI workflow consolidation separate thread; coordinate before changing workflow again.
- **PR #108 hardware-oriented artifact**: successful Windows build run https://github.com/masarray/arsonmix/actions/runs/40593457359; GUI smoke success https://github.com/masarray/arsonmix/actions/runs/40593457208; Action artifact `ArSonMix-GUI10CH-P012-Verified-Hardware-FAT-Windows-x64` (artifact ID 12773724809). **Artifact contains the existing single-channel native app plus non-live P1A core; do not interpret its successful packaging as all ten channels functioning.**

## 1. Exact work COMPLETED in this thread

### A — UI and FX Rack draft in PR #102; not merged

- All empty FX insert slots labeled `＋ ADD FX` (not em-dashes), existing plugins display VST3 names, bypass and editor.
- DAW-like drag gesture: held plugin ghost follows pointer; insertion marker identifies target; **one native move command only at drop**. Pointermove listener only active during gesture. Keyboard/controller fallbacks should be retained.
- `FxOrderPlan`: packed 64-bit logical insert order; callback reads one order snapshot per audio block without shifting VST3 ownership or restarting callback. Graph changes need an audible fade/de-click and conflict-safe acknowledgement; branch has an FX move fade, but user **still reported momentary crackle** after drops. This is NOT hardware-qualified.
- Orbit-associated FX row visual distinction (`has-orbit`), right-click menu with material-style bin Delete FX, plugin catalog popover anchored to selected slot with arrow/selected-row highlight, no screen-wide blur.
- Flat/milled fader visual revised toward four-sided curved sheet-metal depression; maintain exact PNG thumb/travel and same physical trough width on ordinary/master strips. User rejected dark rectangle / glowing white border. Visually verify on real Windows DPI before claiming success.
- JUCE single app instance guard prototype to raise first instance instead of contending on WebView2/audio device. User had experienced Windows freeze with two EXE instances; guard alone does NOT prove root cause fixed.
- Real DSP remains CH1-only on this branch. CH2–10/MASTER inserts are **not** live. Do not present a functioning picker as proof of native channel processing.
- Before porting any #102 feature, diff against modern main, especially PR #105 DearVR POLAR, #106 meter ballistics, #107 sample-accurate FX ON/OFF ramp; resolve collisions manually. Don't cherry-pick entire `src/Main.cpp`, `src/LiveAudioEngine.h`, `web/bridge.js` or UI CSS wholesale.

### B — REAL 10-channel routing foundation in PR #108; tested but not live

`engine/core/MixerRoutingPlan.h` (new):
- Ten source strips + one MASTER, all disconnected by default, explicit physical inputs per strip. Source modes `disconnected`, `mono`, `stereo`; no hardcoded GUI label == audio endpoint.
- Off-RT compile verifies physical index bounds, duplicate stereo inputs, unknown modes, finite gain/pan, and full-plan rollback on invalid configuration.
- Precomputed equal-power **mono** PAN and **stereo balance**; MUTE/SOLO arbitration, channel gain, master gain/pan/mute/mono.
- `extract` copies physical input into caller-owned stereo scratch; `accumulate` sums independent post-FX strip buffers onto master; `finishMaster` handles final master controls. No heap/locks/per-sample trigonometry in these DSP primitives.
- Unrouted/unavailable inputs fail closed to silence; no fake meter or VST3 support.

`tests/mixer_routing_plan_test.cpp` tests ten independent input samples, mono/stereo mixing, PAN, MASTER controls, SOLO/MUTE, invalid inputs, rollback. Existing CMake Windows CI builds `ARMixMixerRoutingTests` and CTest. Doc `docs/P1A_REAL_MIXER_ROUTING_CORE_20261028.md` explains scope.

**Confirmed results:** PR #108 Windows build/CTest **SUCCESS** run 40593457359, standalone GUI/geometry **SUCCESS** run 40593457208; Action artifact generated. PR remains **unmerged** and in Git conflict because main advanced (now includes PR #107). No physical realtime qualification has been carried out for this P1A core.

## 2. What is NOT DONE (no artificial green status)

1. Issue #15 0-FX crackle not certified solved on actual ASIO/WASAPI hardware. Reported drop/apply FX crackle also lacks passing hardware A/B evidence.
2. PR #108 must be conflict-reconciled into current main. It contains two NEW files plus edits to `CMakeLists.txt` and existing Windows CI, and its docs. Preserve new PR #107 de-click CMake/CI targets. **Manual port of focused changes is preferable to force-pushing or blanket merge**.
3. P1B: `SourceRegistry`, physical mono/stereo channel discovery, user-selectable source for EVERY strip (à la Adobe Audition/Studio One), routing from other strips or buses (controlled sends), mapping by endpoint IDs, sample-rate/format negotiation, explicit disconnected state and hotplug recovery. Stereo pair and mono semantics must be unambiguous.
4. P1C: real native `ChannelRuntime[10]` plus `MasterBus`, independent VST3 insert chains, FX Return and safe sends; `Input → per-channel FX → gain/pan/mute/solo → master sum → master FX → output`, plus pre/post true meters. Avoid loops, double stereo gain, aliasing and silently processing wrong input.
5. Immutable graph/order publication at audio block boundary, preallocated scratch with bounded CPU (active channels only), off-RT plugin creation/destruction/state loading; no callback allocation, blocking lock, logging, UI rendering, device restart, editor creation or re-entrant vendor processor calls. Use already-proven `LiveGraphHandoff` epoch/retirement fence as lifecycle model; preserve ASIO/direct/independent-WASAPI safeguards.
6. Stable `channelId/insertId` independent of slot index. Per-channel Add/Remove/Move/Bypass/Editor/Orbit, command revision/ACK/error/rollback, snapshot recall and portable VST Vault. Orbit association must follow instance as it moves. Realtime gain/pan must update without rebuilding graph on every fader tick.
7. GUI can enable `＋ ADD FX` for each channel and MASTER **only when that corresponding native bus is actually routed/implemented**. Catalog should point to clicked source+slot. Empty inserts become plugin names after **successful native ACK**, with true bypass and actual audio. Retain ghost drag, right-click delete, muted-but-visible Orbit marker, responsive and low-GPU CSS.
8. End-to-end Windows FAT: real multi-source isolation, two-channel VST, snapshots, plugin editor handling, simultaneous tracks, CPU deadline and worst-case latency, FX order change no pop/click, no baseline 0-FX crackle, 1 vs 2 instance launch, Windows 125/150% DPI and low-end host. A green CI cannot substitute for measured hardware.

## 3. Suggested implementation order — safe milestones

**Gate 0 / reconciliation FIRST:** Fresh read of main and all open PRs. Diff #108 and #102 against current main. Preserve #105/#106/#107. Do not import unrelated old drafts. Do not burn multiple CI runs on cosmetic edits.

**Gate 1 / P1A integration:** On *fresh main* port only `MixerRoutingPlan.h`, `mixer_routing_plan_test.cpp`, targeted CMake registration and Windows test target, and one architecture doc. Resolve conflicting CMake/workflow by hand. Run focused C++ test locally where feasible before one GitHub CI batch. Mark PR #108 superseded/closed only after merged equivalent is verified.

**Gate 2 / P0 audio reliability:** Follow `docs/P0_00_AUDIO_ISOLATION_RUNBOOK.md`, Issue #15, `docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md`. Obtain real driver + output test at 48k/44.1k 0 FX; label callback gaps correctly, track usable backend starvation or capture flags. Fix only root-cause evidence, not speculative buffers.

**Gate 3 / P1B source registration:** Real endpoint IDs, mono/stereo device channels and router snapshot; tests for missing sources/device swap and no cross-strip leaks. UI source selectors eventually replace demo labels with real mappings.

**Gate 4 / P1C realtime buses:** Implement safe 10 × channel DSP + master in prepared graph with true VST3/Orbit/portable state. Test isolated routes and stereo plugins first, then simultaneous channels; ensure no callback unsafe operations.

**Gate 5 / FX UX + live safe mutation:** Port selected proven #102 UI changes **after** verifying modern main; check sample-accurate FX bypass (#107). Test drag/drop, state continuity, acoustic fade & latency, click-free mutation under load.

**Gate 6 / hardware release:** Build provenance and asset parity, plugin load stability, ASIO/WASAPI real hardware, no clicks/glitches, documented measurements. Only then describe app as fully functional professional live mixer.

## 4. Non-negotiable engineering acceptance

- All 10 source channels can independently choose mono single-channel, stereo pair or a permitted bus source; MASTER own FX chain; no fabricated active audio; user can patch/send flexibly without feedback.
- Identical FX Rack semantics on every bus: all empty slots **＋ ADD FX**, plugin names/bypass when ACKed, true drag/drop and delete, per-source plugin editor, audible routing.
- No extra GUI filter/blur/glow/repainting on pointer hover; selected toolbar olive-green; subtle metal-fader trough with top/bottom edges and identical dimensions for MASTER; original thumb and dB geometry preserved.
- No random device restarts under drag/bypass/insert/recall; smooth event-limited click suppression backed by measured recordings/diagnostic windows, not vague claims.
- Bounded callback, de-aliased processors, no allocation/lock/OS/device operations, race-free graph retirement and immutable order across blocks. Regression coverage for Orbit/dearVR, fader/meter, single-instance, portable VST import/backup/restore.
- Public/private builds have same functional capabilities; all users may bundle their own legally acquired VST3 and presets in their personal workspace — public distributable must not include the owner's private VSTs.

## 5. Exact next-thread prompt (copy into new ChatGPT GitHub thread)

> GitHub repo `masarray/arsonmix`. Read **`docs/HANDOFF_ARSONMIX_FX_ROUTING_20261028.md`** on branch `docs/handoff-arsonmix-routing-fx-20261028`, then read latest `main`, PRs #108/#102/#101/#97 and issues #95/#15 before writing code. `main` moves concurrently across threads: compare SHAs and do not overwrite new code. PR #108 has real 10-strip routing core and Windows CI PASS but is **not merged** and is dirty vs main; PR #102 has true FX ghost drag/popover/Orbit markers/context delete and atomic FX reorder but is also dirty vs main and has unverified FX-drop crackle. The latest `main` includes PR #105 dearVR POLAR, #106 meter ballistics, #107 sample-accurate FX bypass de-click. First reconcile P1A core cleanly into fresh main while preserving #107 build/test targets, then continue P0 zero-FX hardware issue #15 and P1B real source selection/multi-bus engine. No fake GUI-as-DSP; don't activate unqualified 10-VST channels. Target optimized, ultralow latency, stable, portable, maintainable, no allocations/locks/restarts in RT callbacks, efficient CI. Make a meaningful verified milestone and report actual PR, CI, and unverified hardware gates.

## 6. Thread closing condition

This conversation/thread can be closed **for coordination** once this handoff is saved and accessible. Closing it does NOT mean the product features, P0 crackle, or multichannel real VST DSP are complete. Successor thread should continue from fresh repository evidence and explicitly record any newly merged work.
