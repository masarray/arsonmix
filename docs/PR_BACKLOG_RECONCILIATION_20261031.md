# ArSonMix — Live Main / PR Reconciliation Ledger (2026-10-31 WIB)
Status: operational source-of-truth for **PR disposition and safe merge order only**. Product architecture continues to be owned by [PRODUCT_TRUTH_CROSS_THREAD_20261031.md](PRODUCT_TRUTH_CROSS_THREAD_20261031.md), the canonical PRD/ARD, and verified executable code. This ledger is not a new architecture or independent roadmap.

## 0. Verified audit checkpoint
- At audit start, latest `main` = **`44376031932447f97a3bc017af499e8205880a12`** (merged documentation PR #227; custom JUCE Audio Device Setup *design only*). Refresh exact main SHA and open PR status **at every merge**; this snapshot is not a timeless branch pointer.
- The latest source-bearing main merge #224 produced `main@27659119ce8c24341edfdd5e5230b4b7c4a0384e`, with Windows MSVC/CTest/package [41160099477](https://github.com/masarray/arsonmix/actions/runs/41160099477) **SUCCESS** and Chromium UI [41160099173](https://github.com/masarray/arsonmix/actions/runs/41160099173) **SUCCESS**. #227 is docs-only and does not by itself validate a new executable/device capability.
- Initial open PR count = **21**. Of these **18 historical/divergent PRs**, **#222** superseded by #224, **#225** four-sibling FactoryFX, and **#226** current CHANNEL/AUX master milestone. After closing #222: **20 open** (18 historical plus #225/#226). New reconciliation PR(s) are temporary and must not create another parallel product implementation.
- Passing CI at an old branch SHA, a GitHub `mergeable` badge, a mock-device UI test or a successfully packaged EXE must never be misreported as real hardware FAT. [#15](https://github.com/masarray/arsonmix/issues/15) **0-VST physical crackling**, loopback latency, sample dropout and wired soak remain unqualified.

## 1. Already MERGED — do not reimplement or resurrect from old branches
| Domain | Merged PRs | What is real / what is NOT claimed |
| --- | --- | --- |
| Realtime core | #203 MAIN+MONITOR, #209 independent user-selected secondary output, #215 DYN1B-2A | One native 10-input/4-AUX live mixer; ChannelId replacement kernel and legacy A/B PCM equivalence are **not live dynamic CH11+** |
| Console | #208 L2 FX/Routing layout, #211 pinned MASTER, #216 RAIL3 | Preserve 11 aligned fader strips, master visibility, dark upper groove; no duplicate UI/controller |
| FX/Show | #188 Factory registry, #197 G5E VST3 Show, #210 source release intake | One JUCE VST3 host; `config/factoryfx-approved-releases.json` currently contains `plugins: []` (no original Factory VST3 binary bundled) |
| Operator routing | #218 PATCH1, #219 MS5, #223 native live AUX POST gain, #224 SOURCE1+real driver/64-pin selection | Existing POST-send gain and guarded MUTED source change operate on native ACK; no unrestricted armed-output quick-switch or independently functioning custom WebView2 driver setup |
| Product/docs | #217 convergence, #220 PATCH2 target, #221 product truth, #227 custom device-setup PRD/ARD | #227 designs replacement of native JUCE setup popup, **not implemented**; SOURCE1 #224 is not full PATCH2 seamless switch |

Do not replace `src/Main.cpp`, `LiveDryRoutingLab`, `web/monitor.js`, `web/mixer-io.js`, standalone GUI resource, `AGENTS.md` or L2 CSS with stale wholesale copies.

## 2. Current PR disposition (only the latest owners)
| PR | Current disposition / acceptance gate | Overlap and safe next action |
| --- | --- | --- |
| [#222 SETUP1](https://github.com/masarray/arsonmix/pull/222) | **CLOSED SUPERSEDED** after auditing file scope against merged #224. Old PR head CI passed but it is not an additional release. | #224 contains device chooser, native 64 pins, bus view, SOURCE1 ACK and corresponding tests; old diff overlaps `Main.cpp`/`monitor.js`/`mixer-io.js`. Preserve PR branch for narrow salvage only, never merge wholesale. |
| [#225 ArSonComp FactoryFX](https://github.com/masarray/arsonmix/pull/225) | Old PR exact-head Windows [41159434568](https://github.com/masarray/arsonmix/actions/runs/41159434568) **SUCCESS**; **draft and not mergeable into moving main without conflict** (GitHub merge rejected 405). | Port ONLY eight unique factory watcher/staging/test/docs additions onto new main. Do not overwrite newer `AGENTS`; avoid attempting stale squash again. The old PR should be closed as superseded **after** a replacement branch preserves its unique changes and passes the relevant gates. |
| [#226 CHANNEL/AUX MASTER](https://github.com/masarray/arsonmix/pull/226) | **DRAFT, implementation NOT merged**; exact old head Windows [41160392121](https://github.com/masarray/arsonmix/actions/runs/41160392121) + Chromium [41160392139](https://github.com/masarray/arsonmix/actions/runs/41160392139) **SUCCESS**. | Real selected-channel SOURCE→FX→SENDS→OUTPUT readback and realtime native AUX master gain; overlaps #222/#224 in `Main.cpp`, `web/monitor.js`, `mixer-io.js`, standalone UI, tests. Reconcile against latest main and #227 **without** losing device setup, live SEND faders, route ACK or Show. Preserve PR until exact integrated SHA CI + FAT plan. |
| **Custom device setup** | #227 **docs merged**; no implementation PR has been approved in this audit. | Implement later as ONE existing JUCE AudioDeviceManager with a WebView2 device UI, preflight/MUTED APPLY/native ACK/readback/rollback. No second callback or fake device inventory. Source docs: PRD/ARD/HANDOFF_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md. |

**Ownership fence:** `#226` owns the selected channel and live AUX master / audio graph. FactoryFX source release intake owns its watcher/staging/test/docs and *does not edit* `Main.cpp`, `LiveDryRoutingLab`, `monitor.js`, `mixer-io.js`, FX graph or audio callback. The custom device panel is a separate future implementation requiring explicit reconciliation with #226 and real ASIO/WASAPI device lifecycle.

## 3. 18 historical OPEN PRs — QUARANTINE / NO BULK MERGE
| Historical PRs | Reason to retain as history; modern owner |
| --- | --- |
| #128 | Old toolbar/audio setup/snapshot concepts; modern source owner #224/#227 and Show #180 |
| #117, #115 | Previous two-AUX and execution graphs; runtime migration #192 DYN1B owns any unique missing behavior |
| #113, #59 | Old handoff/reconciliation snapshots; this ledger and PRODUCT_TRUTH supersede their status |
| #60 | Experimental meter canvas; only use measured low-spec Windows iGPU evidence in #56 |
| #29, #26, #25 | Dual-clock/recovery experiments; real hardware #15 and OUTPUT1 own verified outcomes |
| #10, #9, #8 | Old VST swap/bus/state work; FX #95 / Show #180 own any audited delta |
| #7, #6, #5, #4, #3, #1 | Chained early native P1/P2 prototypes; never graft stale callback/GUI files into current main |

These 18 historical PRs remain available as audit material. Do not bulk-close or merge without unique-delta assessment. They are **not** 18 executable tasks in the current sprint; their source branch CI state cannot overrule latest main.

## 4. Factory VST3 cross-repo truth (2026-10-31)
- Four **implemented sibling sources** targeted next for default FactoryFX: ArSonEQ (feature branch; no public release, embedded BUILD SHA mismatch previously found), ArSonComp (`masarray/arson-comp` draft PR #1, latest observed head `673c2ca9`, Windows VST3 engineering artifact; no public release), ArSonVerb (VST3 source on main, no official GitHub Release; confirm outbound licence), and ArSonKuPik Enhancer (public binary distribution from `masarray/vst-enhancer` v0.5.26; entitlement/redistribution approval unresolved).
- The final six first-party product target adds future Harmony and Color. Do not claim built-in binaries simply because a repo contains source or CI is green.
- The reviewed approval lock is **EMPTY**. A new upstream release is detected into a candidate PR, then separately admitted with exact SHA256, source commit, VST3 class ID, actual JUCE host UID, licence+notice, host validation and rollback. No live binary replacement during audio operation.
- ArSonComp actual artifact uses `Plugin/ArSonComp.vst3`; FactoryFX packager must support this and `VST3/<bundle>` safely while excluding `Standalone`. Source/zip tests must travel with the packager. Source-specific acceptance: [arson-comp #2](https://github.com/masarray/arson-comp/issues/2).

## 5. Canonical release / merge sequence
1. **M0 hardware P0 #15 remains in parallel and blocks product certification.** Use latest exact source-bearing successful Windows artifact; wired ASIO/WASAPI 44.1/48k, 0 VST, real source/output PCM and 30-minute clean soak. Capture reproducible health trace. No zero-total-latency claim.
2. **Merge self-contained FactoryFX reconciliation ONLY after one branch based on latest main contains the eight source changes + targeted offline tests + successful exact-head Windows gate.** It must not touch the live audio controller. Close old #225 as superseded after that branch is verified/merged; retain history.
3. **Rebase/reconcile #226 selected-channel/AUX native milestone** against actual new main, #224 and #227. Same native ACK status authority, one continuous DSP callback, no duplicate route manager. Pass Windows + Chromium same-head and inspect L2/1366px. Only then merge; hardware FAT still separate.
4. **Next milestone: genuinely custom WebView2 Audio Device Setup** from merged #227 ARD/PRD, not cosmetic button duplication. Test device selection and rollback on actual hardware. Coordinate native owner with #226.
5. **Next whole-product milestones:** PATCH2 safe live source switch, full ChannelId one-graph cutover then runtime +CHANNEL, native all-channel/MASTER VST3/PDC, recall-safe Shows, legal factory VST3 binaries, field release qualification. Never split into parallel competing DSP systems.

## 6. Merge checklist — use this before EVERY PR
- [ ] Refresh GitHub `main` SHA, open/merged PRs and exact PR head; no cached status.
- [ ] Declare ONE owner/milestone and exact file overlap with open native/UI PRs.
- [ ] Confirm functionality not already merged (include native PCM/ACK proof, not only GUI).
- [ ] Rebase or selectively port against actual current main **without overwriting recent docs/AGENTS or GUI**.
- [ ] Run targeted offline/static tests first; then only applicable native Windows CTest/ZIP and Chromium test for exact integrated SHA.
- [ ] Preserve existing L2/RAIL3/master, ROUTE/FX separation, plugin Show identity, MAIN/MONITOR/OUTPUT1, SOURCE1 and realtime POST sends.
- [ ] No audio-callback allocation, filesystem/network I/O, UI calls or blocking locks; structural changes remain guarded, continuous level changes atomic/dezippered.
- [ ] Distinguish MERGED / SOURCE-BUILT / CI PASS / VERIFIED PACKAGE / PHYSICAL FAT PASSED / RELEASE QUALIFIED.
- [ ] Update this PR disposition ledger + product truth only where product behavior genuinely changed. Do not open a new unrelated handoff for every tiny task.

Next-thread starting files: `AGENTS.md` top directives -> `docs/PRODUCT_TRUTH_CROSS_THREAD_20261031.md` -> **this ledger** -> latest PR/head/CI -> appropriate feature PRD/ARD/handoff -> source & tests. GitHub source and actual device evidence always outrank this dated ledger.
