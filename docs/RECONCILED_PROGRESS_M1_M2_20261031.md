# ArSonMiX — M1/M2 release & PR reconciliation (31 October 2026)

**Canonical decision record, snapshot only.** Baseline reviewed: `main@7db4ab70a4c971487e0b921df5a4e8cc805f0183`, after #234 merge. Previous cross-thread product truth documents reflect earlier milestones and **must not overrule this newer factual ledger**. Refresh real CI and main HEAD before decisions. ArSonMiX is a low-latency *realtime Windows digital mixer* for physical or virtual I/O, live karaoke, stage, podcast and livestream — **not** a timeline editor, WebAudio engine, OBS-only mixer, or zero-millisecond hardware promise.

## 1. Source of truth: MERGED and software-verified

| Area | Established result / authority | What this does NOT prove |
| --- | --- | --- |
| Physical interface/device setup | #224 (selectable 64 JUCE pins), #228 (actual custom WebView2 ASIO/WASAPI/DirectSound driver/endpoints/rate/buffer/pin setup, muted native APPLY, ACK, safe rollback), #234 (DEVICE2 cached inventory + explicitly guarded REFRESH + exact active hardware mask/rate/buffer readback) | No proven guarantee against vendor-driver in-process hang; 64 selectable hardware pins is not 64 live mixer strips |
| Channel source | #218 native input matrix; #224 muted fast SOURCE; #233 selected-channel SOURCE2 directly chooses verified capture pins with one native route ACK; #234 contains DEVICE2 **and** preserves #233 | No universally clickless live-ARMED source switching; no arbitrary simultaneous multi-interface capture; no stable named logical source registry |
| Live mixer and bus | Current one JUCE callback/accepted routing graph: **10 input strips + 4 internal AUX returns**, native sends and AUX master, MAIN, independently selectable MONITOR hardware pair and optional secondary output with clock bridge (#203/#209/#223/#231) | No user-created CH11+, full arbitrary BUS/MATRIX/DCA/CUE, dynamic graph cutover or automated latency/PDC proof |
| GUI | Existing L2/RAIL3 compact 11-strip console and one WebView2 AUDIO I/O matrix; selected CHANNEL focus and SOURCE/AUX workflow, no second audio setup window | Not yet comprehensively qualified on low-end iGPU, vendor hardware or large plugin loads |
| VST3 and Shows | Existing native host with CH1 legacy FX and routed CH/AUX insert/Show leasing where accepted | No common CH1..N VST ownership/MASTER insert equivalence, global Studio One feature parity or zero plugin-added latency |
| Build & source audit | #234 exact-head Chromium [41173141028](https://github.com/masarray/arsonmix/actions/runs/41173141028) **SUCCESS**, Windows native/CTest/verified ZIP [41173140873](https://github.com/masarray/arsonmix/actions/runs/41173140873) **SUCCESS**; merged commit above | CI is NOT a physically audible MAONO or WASAPI FAT |
| Downloadable FAT before #234 | Last verified available `main@a7fb61511ffe27fdef1f9923695daf6899646dff` Windows x64 artifact at [41166662476](https://github.com/masarray/arsonmix/actions/runs/41166662476), artifact `ArSonMix-GUI10CH-P012-Verified-Hardware-FAT-Windows-x64` | This ZIP **does not yet contain #234**; use a successful **newer main** Windows artifact when available |

**Native invariants:** ONE `AudioDeviceManager`, ONE native callback, ONE routing authority (`web/monitor.js` + native epoch/revision/ACK), SINGLE WebView2 matrix, no JS DSP, explicit structural MUTE → APPLY → native readback/ACK, never auto-arm unknown output. Fader, mute/solo, pan and already-built AUX send gains are ordinary RT control operations, not forced topology rebuilds. Do not silently substitute a different microphone, speaker endpoint, virtual pin or clock.

## 2. Canonical PR ownership — do not double-merge

| PR | Current decision at this snapshot | Next gate |
| --- | --- | --- |
| [#234](https://github.com/masarray/arsonmix/pull/234) | **MERGED**. Integrates DEVICE2 #232 onto selected-channel SOURCE2 #233 on main. No new DSP manager; future device edits start here | Re-verify main CI + real hardware |
| [#232](https://github.com/masarray/arsonmix/pull/232) | **CLOSED, NOT MERGED** after #234. Historical old-head source; superseded and should never be bulk-merged | None; only retain tests/evidence |
| [#235](https://github.com/masarray/arsonmix/pull/235) | **OPEN, NOT YET ACCEPTED**. M2 proposal unifies FX rack presentation across CH1–10, adds bounded subgroup/FX BUS *using spare existing native CH*, still separates CH1 legacy and routed VST ownership | Fix exact-head Chromium smoke; earlier runs failed on old static assumptions and on `tests/ui_smoke.py` FX-picker visibility/count (latest investigated failure line 1464 in run 41173956993). Reconcile/rebase over #234, then exact-head UI + Windows CTest + hardware FAT. **No premature merge** |
| [#229](https://github.com/masarray/arsonmix/pull/229) | **OPEN, STALE BASE**. Reconciles FactoryFX supplier/Comp packaging, docs and PR backlog with more recent upstream than #225; not related to DEVICE or ROUTE graph | Rebase only nonoverlapping FactoryFX admission/watcher/test/docs from current main. Require approved legal SHA lock and offline test; no auto-shipped FX |
| [#225](https://github.com/masarray/arsonmix/pull/225) | **OPEN, STALE**. Original ArSonComp supplier watcher/staging integration overlaps #229 | Never merge both old branches as-is. Prefer a single reconciled FactoryFX owner on current main; close the redundant PR only when replacement is actually merged |
| Old PR #128/#117/#115/#113 and pre-M1 branches | Historical prototype work, not authority over current merged native one-graph/session architecture | Quarantine; selectively audit missing requirements, never bulk-merge divergent `Main.cpp`, `monitor.js`, FX/GUI |

## 3. Defects / high-value next milestones

**P0 — Physical signal FAT and issue #15.** Zero-VST crackling remains unresolved. On real Windows hardware test **0FX first** at driver-reported 44.1/48 kHz and 64/128/256 if available; inspect input vs output discontinuity, callback gap, true audible dropout, latency and loopback; preserve source-labeled audio report + MARK CRACKLE. Test at conservative headphone volume. Real ASIO registration, pin enable masks, default rate, MAIN/MONITOR/OUTPUT1, disconnection and accepted native route ACK all need operator evidence. An artifact ZIP is software-tested, not sound-tested.

**P1 — Finish M2 correctly, not cosmetically.** First make the CH1–10 shared FX presenter and real BUS/FX BUS staged wizard pass Chromium without breaking normal CH1 VST ADD, EDIT, BYPASS, drag order, VST lease or Show. A proposed BUS consumes a genuinely unused one of ten native channels, never fabricates CH11; next major architecture step is unified dynamic `ChannelId` realtime graph and VST ownership/PDC, separately gated.

**P1 — Driver responsive architecture.** #234 avoids scanning all vendor drivers on every Audio Setup open, but explicit refresh or actual driver switch remains synchronous JUCE vendor code; a worker on the same manager is not inherently safe. Only after measuring field stalls evaluate isolated **inventory** helper process or separated host architecture. Avoid a naive async wrapper or claim "zero freezing."

**P2 — Factory VST3 distribution.** `config/factoryfx-approved-releases.json` currently contains **`plugins: []`**. Until auditable source release, exact SHA256, publisher permission, VST class IDs, host qualification and compatible packaging are supplied, do not present ArSonComp/EQ/Verb as shipped by default.

## 4. Acceptance and CI economy

1. One functional owner per domain (DEVICE: #234; SOURCE: merged #233; M2 console/rack/bus: #235; FactoryFX: reconcile #229/#225), one accepted main state; rebases onto current main, never resurrect stale branch engines.
2. Source/quick tests first; use a **single stable head** for full Windows MSVC/native CTest/package + Chromium run. Do not repeatedly amend a PR while Windows compile is underway or start duplicate workflows.
3. Merge only after exact-head green UI/native gates, no conflicts with newly merged source/device owners, and clear negative/fail-closed tests. Then main publishes the verified user-downloadable Windows artifact.
4. Physical release remains gated by ASIO/WASAPI FAT and #15 even after all green software CI.
