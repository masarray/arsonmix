# ArSonMix — CLEAN REPOSITORY AND SINGLE EXECUTION PATH
**Reconciled:** 2026-11-01, main before documentation commit `0012b6a0fd20bebdadc68cbcb9f056083fbe5836`.
**Verified API snapshot:** 24 open PR and 21 issue-only open tickets **before** cleanup → **4 open PR and 8 issue-only open tickets AFTER**. All 20 obsolete PRs and 13 redundant/historical issues were closed with individual reasons and pointers. Closed != merged, fulfilled, deleted or code discarded. Branch commits and past conversations remain available.
**Authoritative specs:** [PRD v5](PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md) and [ARD v5](ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md). Supersede obsolete v2–v4 instructions. Historical metrics are date-scoped.

## 1. Four open PRs — purpose, isolation, exact gate

| PR | Current authority | HEAD audited | CI / qualification | Merge decision |
|---|---|---|---|---|
| [#266 M-U1 native graph admission](https://github.com/masarray/arsonmix/pull/266) | **PRIMARY engine integration branch** | `2bbf9f08b549` | [Windows MSVC/CTest RUN 41252951831](https://github.com/masarray/arsonmix/actions/runs/41252951831) **SUCCESS** on this commit. Native processor/PCM tests; no real hardware FAT | Continue *within this branch*: external CH1/Orbit lease, native Show, callback switch, approved output, device restore. **DO NOT claim M-U1 done or release** merely from the tests. Retain draft until a full-fidelity callback cutover or explicitly label a limited foundation merge. |
| [#265 R7 channel/source/GUI](https://github.com/masarray/arsonmix/pull/265) | **Single UI integration branch** | `d57c3431a52c` | Latest [Chromium RUN 41250145548](https://github.com/masarray/arsonmix/actions/runs/41250145548) **CANCELLED**; prior regression tests failed on changed one-APPLY semantics. No exact-head green UI CI | Rebase onto latest main, reconcile changed source/format ACK tests and native control authority. Do not merge broken smoke or pretend a UI-format flip is live PCM. Preserve approved fader geometry and standalone parity. |
| [#251 WASAPI capture de-click](https://github.com/masarray/arsonmix/pull/251) | **ISOLATED P0 experiment**, converted to DRAFT | `291886812d4a` | Opt-in/default OFF; no matched physical ASIO/WASAPI evidence; **#15 unresolved** | **NO MERGE** until physical 0FX soak and verified capture vs render discontinuities plus safe rejection of legitimate silence. |
| [#225 FactoryFX ArSonComp intake](https://github.com/masarray/arsonmix/pull/225) | Separate plugin packaging/distributor owner | `12ed338e335a` | Release candidate/provenance and native host tests still needed; approved production FX manifest empty | Park as DRAFT. No factory plugin binary shipped merely from metadata; requires signatures/rights/manifest/CID/host FAT checks under #185. |

**PR 263** independent dry-only Group: **CLOSED superseded**. Its single callback pilot rejects any normal FX/AUX/MONITOR/Show, conflicts with #266 and does not establish production Group parity. Reference tests selectively, never merge the obsolete patch.
**PR 229** FactoryFX reconciliation: **CLOSED duplicate** of #225 plus superseded documentation. Do not work in two FactoryFX branches.

### Archived PR ledger (20 closed without merge)

- Legacy P1/P2 stacked chain: [#1](https://github.com/masarray/arsonmix/pull/1), [#3](https://github.com/masarray/arsonmix/pull/3), [#4](https://github.com/masarray/arsonmix/pull/4), [#5](https://github.com/masarray/arsonmix/pull/5), [#6](https://github.com/masarray/arsonmix/pull/6), [#7](https://github.com/masarray/arsonmix/pull/7), [#8](https://github.com/masarray/arsonmix/pull/8), [#9](https://github.com/masarray/arsonmix/pull/9), [#10](https://github.com/masarray/arsonmix/pull/10).
- Old split-device / legacy performance experiments: [#25](https://github.com/masarray/arsonmix/pull/25), [#26](https://github.com/masarray/arsonmix/pull/26), [#29](https://github.com/masarray/arsonmix/pull/29), [#60](https://github.com/masarray/arsonmix/pull/60). None authorizes second DSP callback or asserts #15 fixed.
- Historical docs, obsolete GUI and offline routing: [#59](https://github.com/masarray/arsonmix/pull/59), [#113](https://github.com/masarray/arsonmix/pull/113), [#115](https://github.com/masarray/arsonmix/pull/115), [#117](https://github.com/masarray/arsonmix/pull/117), [#128](https://github.com/masarray/arsonmix/pull/128).
- Overlapping recent branches: [#229](https://github.com/masarray/arsonmix/pull/229), [#263](https://github.com/masarray/arsonmix/pull/263).

Closing a stale branch is **not** source deletion. Retrieve isolated tests or engineering insights from its original commit, and apply them in current owner branch with new baseline + regression. **No mass merge, cherry-pick or force push.**

## 2. Eight authoritative issue-only tickets

| Issue | Scope / no duplication |
|---|---|
| [#192](https://github.com/masarray/arsonmix/issues/192) | **P0 MAIN EPIC:** M-U1 production ChannelId full-fidelity cutover, M-U2 true CH11+, Group Selected and FX Return, one-gesture UI/native ACK, M-U3 safe online structural edits and PDC. Only issue controlling native engine and generic console routing. |
| [#15](https://github.com/masarray/arsonmix/issues/15) | **P0 RELEASE BLOCKER:** acoustic zero-FX crackle and device/clock/direct-monitor investigation with real MAONO/ASIO/WASAPI/virtual endpoint evidence. Must not fake hardware pass from Windows/Chromium CI. |
| [#176](https://github.com/masarray/arsonmix/issues/176) | **GUI BASELINE regression guard:** compact 7-row aligned faders/MASTER, no blur, bank and meter geometry, low-iGPU pointer responsiveness. Not an extra redesign PR. |
| [#180](https://github.com/masarray/arsonmix/issues/180) | Full Show/Scene/Recall Safe, plugin state, dynamic ChannelId persistence, guarded hardware remap. Owns native persistence semantics; coordinate M-U1 schema migration under #266. |
| [#185](https://github.com/masarray/arsonmix/issues/185) | Approved six native first-party FactoryFX releases, manifests/notice/provenance and JUCE host acceptance; #225 is its ONE PR branch. |
| [#85](https://github.com/masarray/arsonmix/issues/85) | User plugin vault, portable all-user product and permission/packaging contract; keep separate from production graph. |
| [#135](https://github.com/masarray/arsonmix/issues/135) | **DEFERRED:** measured LUFS-I/4x true peak with native meters, never sample-peak disguised as dBTP. No diversion from M-U1. |
| [#44](https://github.com/masarray/arsonmix/issues/44) | **DEFERRED:** multi-theme appearance token model, no layout ownership, no GPU-heavy paints. |

**Closed 13 overlap/historical issue-only tickets (not feature-completed):** [#205](https://github.com/masarray/arsonmix/issues/205), [#182](https://github.com/masarray/arsonmix/issues/182), [#179](https://github.com/masarray/arsonmix/issues/179), [#175](https://github.com/masarray/arsonmix/issues/175), [#174](https://github.com/masarray/arsonmix/issues/174), [#173](https://github.com/masarray/arsonmix/issues/173), [#170](https://github.com/masarray/arsonmix/issues/170), [#149](https://github.com/masarray/arsonmix/issues/149), [#137](https://github.com/masarray/arsonmix/issues/137), [#125](https://github.com/masarray/arsonmix/issues/125), [#109](https://github.com/masarray/arsonmix/issues/109), [#95](https://github.com/masarray/arsonmix/issues/95), [#37](https://github.com/masarray/arsonmix/issues/37). Requirement text is retained, but authority is delegated to #192, #15, #176, #180 and latest v5 design. GitHub state reason is DUPLICATE/SUPERSEDED, not done.

## 3. Truth table — baseline code and operator correctness

- **LIVE in current main 0012b6a0:** one JUCE callback fixed **10 input + 4 AUX**, VST3 on native patched fixed channels, special occupied CH1 external VST3/Orbit path, MAIN/MONITOR + optional secondary consumer, FAST3 guarded one-APPLY, internal DSP mono/stereo formats and some native L/R meters. FactoryFX bundled lock remains empty. Browser format/meter fixed-index bugs are not yet repaired in main.
- **#266 M-U1 code-only candidate:** actual M4 vendor FxInstance owner leases + ChannelId-keyed `DynamicChannelFxLeaseBank`, `UnifiedLegacyProductionAdmission` PCM/FX/MONITOR tests. **LiveAudioEngine callback is not switched**; external occupied CH1 VST/Orbit explicitly rejected; Show/editor/device-ARM parity not done. Windows exact-head green is valid *test evidence* only.
- **#265 R7 GUI candidate:** channel M/ST toggle, accepted mono/dual meter projection, bank paging, source one-gesture. No CH11 generated, no universal active plugin before source. **Chromium exact-head not green**; blocked.
- **#251 de-click:** exploratory, DEFAULT OFF; #15 real dry glitch and any driver clock-domain xrun remain unresolved.
- **#225 FactoryFX:** release watch metadata and manifest work; not proof of installed/approved plugin bundle.

## 4. ONE integration path and stop conditions

**ENGINE OWNER #266 — M-U1 (no new parallel PR):** continue CH1/Orbit and all real M4 FX leases, Show/vendor chunks, 10+4 PCM + physical MAIN/MONITOR, exact hardware pin topology, optional secondary, and one callback selection. Test real plugin migration with no allocation/locks in callback, rollback/stale epoch, sample compare, exact-head Windows native and output FAT candidate. Reject dry-only Group fallback. One control-thread graph pointer and safe retirement. Do not add CH11 GUI before this.

**GUI OWNER #265 — R7 (no new overlapping PR):** update to current main and fast/negative ACK workflow, fix failing/cancelled Chromium smoke, 1/2 native meter on ANY input channel, no fake true FX on unpatched CH. Bank navigation and +CHANNEL skeleton only when real dynamic native ACK capability exists. Full named source selection must be retryable after rejected MUTE/route/ARM. Preserve source-owned standalone and L2.

**AFTER M-U1 — M-U2 on same canonical graph (single substantive integration):** CH11 true native processing, Group Selected with already FX-bearing sources and AUX/MONITOR/Show retained, separate genuine FX Return and independent mixer bank; physical output user choice. No reuse of input slots as bus, no second DSP callback.

**M-U3 later:** safe online prepared graph swaps without user global MUTE for supported software edits, MASTER FX, PDC, Cue/Scenes and format-aware sidechain. Failed unsafe operations preserve accepted sound or reject specifically with REQUIRES_DISARM.

**P0 FAT #15 parallel:** 0FX 30-minute ASIO/WASAPI clean capture→postfader→MAIN audible speaker; check actual fader, channel mute and MASTER MUTE stop sound. Separate capture input discontinuity vs render starvation vs virtual/BT device clock. Then real 1/multiFX and hotplug. No stable/zero-physical-latency claims from CI.

### PR gate — no false progress

For any code PR: latest `main` and PR diff; one owner per file; exact base/head; explicitly state callback actually executing and what is only control/offline. Need target-specific native CTests + JS Chromium and any relevant Windows packaging run with exact head SHA. Hardware release claim only after #15 FAT. **Do not force everyone to await new documentation or open duplicate issue/PR.** Docs-only commits do not need expensive CI. One run after meaningful integration, not many redundant PRs.

**Workspace status on completion:** Open PR=4 (all DRAFT); open issue-only=8. Source commits and histories intact. Ready for controlled implementation of #266 and #265—not a declaration that Studio One/LV1 parity exists.
