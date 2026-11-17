# ArSonMix — Non-Blocking Parallel Development & Incremental Architecture Implementation

**Status: PROPOSED WORKING GUIDE — NOT A SOURCE LOCK, NOT A CI ADMISSION POLICY, NOT A NEW AUTHORITY.**  
**Audit:** 2026-11-18; `main@ef7a29933109020657accfb957b7a6ffc8d58cd7`. Always refresh live branch/PR state.  
**Owner/user constraint:** The former four-lane file ownership and collision gates caused deadlock and were **explicitly retired** on 2026-11-05 in [AGENTS.md](../../AGENTS.md). **Never reintroduce them.**  
**Architecture authority:** [maintainability epic #527](https://github.com/masarray/arsonmix/issues/527) and the **already active** [PR #763 Modular MVVM ADR](https://github.com/masarray/arsonmix/pull/763), alongside its multi-device PRD/ARD. This guide is a *contribution/integration supplement*, not a competing architecture ADR.  
**Related feature work:** [#760 ImmerSIB corrected field FAT](https://github.com/masarray/arsonmix/pull/760), [#757 narrower input-only study](https://github.com/masarray/arsonmix/pull/757). No changes to their branches or release state.

## 0. Explicit decision: coordination MUST NOT block coding

**A thread may immediately work on any file/module**, including `src/Main.cpp`, `CMakeLists.txt`, native PCM, GUI, docs or FX, provided it works on its own branch and follows actual source/test safety rules. There is:
- **NO file/feature/domain lock**, reservation, exclusive owner, ownership tokens, claim expiry or PR permission system.
- **NO requirement to obtain approval/handoff from another thread** before writing, running tests, creating a PR, or continuing independent code.
- **NO rule that an open/draft/stale PR makes any other PR unmergeable.**
- **NO automatic merge failure because two PRs modify the same file, directory, 'runtime domain', or issue.**
- **NO new GitHub API-dependent collision CI job.** Missing API access, ambiguous PR metadata or unavailable old branch is informational only, never grounds for a CI rejection.
- **NO mandatory CODEOWNERS approval, merge queue, architecture committee, dedicated integration branch, or synchronizing all active threads.**
- **NO new versioned message bus, state store, secondary DSP graph, duplicated 'source-of-truth' manifest or UI framework** merely to make work look modular.

**Integration responsibility is not file ownership or a queue.** The developer who merges a particular PR reviews and reconciles the final product behavior of *that PR*; nobody acquires exclusive edit rights. Multiple independent PRs can merge independently once the normal required **functional/CI** checks pass. A blocked old research PR must never freeze new development.

Two distinct kinds of safeguards:
- **Never block just for collaboration metadata:** author label, source path, historical overlap, lack of owner response, old workstream, branch age, advisory risk score.
- **Do block actual unsafe/broken changes:** failed required scoped tests, known output auto-arm/identity regression, duplicate realtime DSP render authority, broken `.smix` migration, missing mandatory distributed built-in FX, missing or stale CI for an attempted merge. These must be demonstrable, specific and reproducible, not inferred from another PR's existence.

## 1. Evidence: old deadlock mechanism is retired, but still misleads

The old `scripts/check_workstream_coordination.mjs` contains `audit()` that rejects missing four-lane metadata, wrong path 'owner', same-file overlaps and even same-runtime-domain work in different files. `tests/workstream_coordination_contract.test.mjs` still verifies those historical rejects.

**Critical finding on current main:** `.github/workflows/ui-smoke.yml` and `.github/workflows/p1-windows.yml` **do not call** `check_workstream_coordination.mjs --pr`. `scripts/ci_select_changed_paths.mjs` merely imports the **pure path-selection helpers** `requiresUiSmoke` and `requiresNativeQualification`. Therefore the historical `audit()` algorithm is **not currently the production PR admission gate**. Its historical unit tests may still run as *tests of old behavior*, but they do not authorize reinstating the old policy.

**Do NOT delete this script wholesale**: that would also remove test-lane selection functions used by active CI. Do not edit CI or protected-main rules merely because the script contains dormant legacy code. When a measured maintenance task justifies it, move the path selectors into a dedicated pure `ci_path_selection` module, update imports/tests, and retire the unused old `audit()` code in the **same tested PR**. This is optional cleanup, not a feature prerequisite or additional gate.

The former `docs/workstreams/MULTI_AGENT_COORDINATION.md` starts with SUPERSEDED but retains a 'binding' heading and paragraphs demanding file owners. This text is a historical artifact. Keep a conspicuous note explaining that *none* of its four-lane instructions are operative; do not turn the document into an action list. The new [PR template](../../.github/PULL_REQUEST_TEMPLATE.md) is a light evidence prompt, not an enforced ownership form.

## 2. Parallel development: discover → independently implement → reconcile just before integration

**Very short, bounded preflight (best effort, not a permission gate):**
1. Read the current `main` SHA, [AGENTS.md](../../AGENTS.md) and only the specific source/contracts for the desired feature; inspect open, **relevant** PRs for already-implemented useful work. This prevents rework; inability to fetch a different thread does not revoke permission to code.
2. Define one observed behavior/defect, stable interface or UI acceptance checkpoint, and one nearest regression test. Avoid generic multi-month architecture spikes in place of a product milestone.
3. Create/use your own branch, implement and test independently. You can edit shared files; Git will not grant special ownership.
4. Before *integrating* a PR, compare to latest `main` and relevant in-flight code, rebase or forward-port with a reviewed diff, then run affected scoped tests. A same-file conflict is a technical merge task, **not a prohibition to work**.
5. Record actual CI result and physical FAT separately; after merge, other threads refresh source normally.

**Source truth:** newest merged accepted code on main is the baseline; an unmerged PR may contain useful unique changes but never automatically overrides main. Accepted user listening/visual/FAT checkpoints constrain product behavior regardless of their original branch. Never copy whole historical `Main.cpp`/CSS/bridge/FX registry to solve a one-line conflict.

**No global 'owner':** one PR has a contributor/integrator who resolves that PR's merge; a second PR can merge first. Branches and features are not serial resources. Short-lived PRs are preferable when feasible, but unfinished substantial work can remain draft **without blocking** other changes. Use feature toggles only when they do not silently disable required public/FAT features.

## 3. Conflict risk is an INFORMATIONAL diff review, not an admission state machine

| Observed overlap | Suggested behavior while coding | What actually determines mergeability |
| --- | --- | --- |
| Two independent built-in FX processors | Both proceed immediately, no coordination approval | Each passes nearest DSP tests, final catalog integration and functional CI |
| GUI styling and device reliability | Both proceed with accepted GUI checkpoint | GUI screenshot/interaction parity plus native state/ACK tests |
| Two branches edit `src/Main.cpp` | Both may work; resolve Git hunks against latest main before merge | Compiled native tests and retained behavior, never a file lock |
| Distinct files change same session/graph ABI | Both may implement separately; exchange interface information if possible | Tests for ID/revision/negative ACK and same authoritative session |
| Historic draft overlaps approved SonKu/ImmerSIB sound | Newer work proceeds; selectively preserve useful unique source | Listening golden/PCM checks, not old draft ownership |
| Multiple distribution/installer changes | Both may develop; integrate flags/catalog and packaging carefully | **Actual** EXE/installer compiled-feature parity and exact source/provenance |
| Source PR cannot read GitHub's stale peer PR via API | Continue isolated development, disclose missing comparison | Git diff/test evidence at merge; no artificial API fail-closed |
| Doc-only architecture analysis overlaps another doc PR | Keep unrelated docs separate, remove duplicate ADR and coordinate by references | Working links, no conflicting claim of canonical spec |

**No automated GREEN/RED blocker**. A warning such as “possible shared graph contract” invites an extra diff/test but is not a CI verdict. Review should never assume that two files in one directory share a runtime contract, nor that distinct filenames imply safe integration.

## 4. Machine-enforced checks should protect the PRODUCT, never developer territory

Existing protected-main required status: `ArSonMix / Required CI Merge Gate`. Keep it, subject to real proof that scoped checks fail on genuine build/contract regressions. This status must be tied to exact candidate HEAD and need only require relevant paths; docs-only work must not trigger paid Windows build. The ruleset is not a thread lock.

**Do not implement a mandatory PR collision analyzer, owner marker parser, reviewer lock, or waiting queue.** Any proposed source-impact reporter is optional, read-only, best-effort and **must not alter commit status**. Do not automatically mark a PR failed because another branch edits the same area, or because GitHub API search timed out.

If considering GitHub merge queue at a future date, first support `merge_group` event checks; the current task does **not** propose activating it. GitHub notes that required checks without merge-group reporting will deadlock queued PRs: https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/managing-a-merge-queue

The only new fitness checks justified in a coding milestone are deterministic **content/runtime checks**: zero additional DSP render owner, no audio callback allocation/lock/UI, correct stable ChannelId/EdgeId and .smix migration, native ACK/installed graph readback parity, compiled mandatory FX/catalog parity, approved GUI output. Introduce each check with a known failing fixture and an independently passing ordinary feature PR; never build a broad regex gate that rejects all legacy debt. Fail-open for *advisory availability*, fail-closed only for *actual product safety/functional tests*.

## 5. Existing architecture is the implementation target (avoid competing ADRs)

Use [PR #763](https://github.com/masarray/arsonmix/pull/763) for the concrete modular-monolith / WebView MVVM feasibility roadmap, PRD and ARD for future multi-device I/O; issue #527 tracks existing M1–M6 extraction. This governance guide intentionally contains **no second architectural authority**.

**Non-negotiable source rules:**
- Keep one native `MixerSessionDocument` / ChannelId/EdgeId logical state, one installed PCM/VST3 **render graph authority** and one approved assembled `/preview/` GUI.
- More than one OS/JUCE callback may eventually exist for additional physical **capture-only/output-only** devices with independent clock domains, but **only one callback performs mixer DSP/VST3 graph rendering**. Do not misread 'one render owner' as 'only one callback process-wide' or duplicate Primary ASIO path.
- Preserve native console-v8 `schemaVersion=6` requestId, decimal uint64 IDs, deviceEpoch, graphRevision, logical-versus-installed ACK and negative/stale handling. **Do not invent `armix.native.intent.vNext` solely for MVVM.**
- WebView ViewModels contain ephemeral presentation/draft/pending state, derived from accepted native snapshot; never a second authoritative JS mixer/FX session.
- Reuse current `NativeDeviceApplyCoordinator`, `NativeLogicalMixerControl`, `DynamicNativeSessionPublisher`, native VST3 leases and approved GUI. A new service class must remove, rather than duplicate, ownership from original code.
- Audio callback cannot allocate, block, scan files, touch DOM/UI, or destroy plugin/editor leases; preparation/graph compile and retirement happen outside the RT callback.
- Preserve the owner-approved SonKu/ImmerSIB/Gain sound/visual baseline. MVVM modernization is NOT a reason to retune audio.

## 6. Practical engineering milestones: incremental, independently releasable

Each work item may begin immediately on its own branch. No item requires another thread to grant edit permission. The order below is suggested for safest integration, not a global freeze.

### Slice 0 — Capture a small baseline, no new gate (read-only, may overlap feature work)

Identify one operator flow currently touched by many modules: DEVICE Apply or WIRE. Record exact source/selector path, existing request/ACK/snapshot, tests and current build profile. Do not demand a separate audited document before feature coding. Use already merged M1–M6 code instead of repeating it. Record a baseline table with: source files changed in the next feature, command-to-ACK timing (if measurable), native processing deadline/underrun (if measured), state recovery and GUI parity. Missing hardware is `NOT MEASURED`, not a launch blocker.

### Slice 1 — One pure Device Setup ViewModel, leave native and GUI intact

**Actual source:** `web/mixer-io.js` currently holds `deviceDraft`, `deviceBusy`, `deviceRequest`, `deviceMatrixWaitEpoch`; `web/approved-native-contract.js` is already a pure `schemaVersion=6` accepted-state reconciler. Do **not** duplicate `monitor.js` route authority.

- Extract *only* device draft/pending/error presentation transitions to a pure small module, e.g. `web/viewmodels/device-setup-vm.js` if the current asset assembler can load it deterministically; preserve existing DOM event binding and all original pixels.
- Inputs: current accepted native device snapshot, UI selection, `requestId`, `deviceEpoch`, ACK result. Outputs: derived selection/disabled/pending/error labels. Native Apply remains sole mutation owner.
- Refactor exactly one UI path; remove original local state transitions being moved. No parallel state store or second command path.
- Test: initial disconnected state, successful Apply only after readback, stale ACK, device disappears mid-flight, rejected Apply retains previous accepted physical binding, no duplicate native command, rescan race and reopening.
- Run pure Node tests first, then scoped approved GUI/Chromium test only for changed view. Native test only if native behavior changes. Roll back module import without session migration or changed user preference.

**Stop if:** source GUI differs from approved `/preview/`, device applies optimistically, or the new VM duplicates `monitor.js`/native accepted state.

### Slice 2 — Thin one native command path only where actual coupling is measurable

**Actual source:** `src/Main.cpp` ConsoleWindow, `src/NativeDeviceApplyCoordinator.h`, `src/NativeLogicalMixerControl.h`, `engine/core/DynamicNativeSessionPublisher.h`. Choose one repeatedly edited device or routing handler, not the whole ConsoleWindow.

- Move validation/prepare/ACK responsibilities **into an existing native application/control owner** and remove the corresponding native-host duplication. Keep WebView event entry and lifetime in ConsoleWindow.
- Keep existing native protocol unchanged; no new vNext envelope, event bus, target framework or extra audio manager.
- Test: cold start without hardware, .smix backward compatibility, repeated/stale command, offline logical status, installed graph readback, SOURCE ONLY -> MAIN, FX slot identity, device restart and rejected/rollback path as appropriate to the exact handler.
- If extracted class only delegates through the old handler, revert: that is code movement with no dependency reduction.

**Stop if:** any physical ASIO render path changes without an explicit audio milestone and native/physical evidence, or new command authority appears.

### Slice 3 — Routing/WIRE ViewModel (separate independently testable product change)

**Existing source:** `web/approved-native-bridge.js`, `web/approved-native-contract.js`, `web/monitor.js`, `web/mixer-io.js`; the native document and graph remain accepted authority.

- Derive native WIRE channels/edges via accepted snapshot, keep only local UI focus, pointer drag and request pending in VM. Remove duplicated projection transitions, not native logical/installed state.
- Verify ChannelId/EdgeId string precision, per-EdgeId PRE/POST/SEND ON, SOURCE ONLY CH meter vs MAIN silence, offline authoring and re-wire, rejected graph/cycle unchanged, FX chain/channel -> BUS -> MAIN, .smix recall, device epoch stale and approved visual gestures.
- Prefer a single coherent integration PR for each *working* slice, not many tiny stacked commits disguised as milestones. Multiple independent feature PRs are still allowed and must not be blocked by a draft refactor.

### Slice 4 — Plugin, device extension, performance and artifact qualification

- Built-in FX extensions consume current central registry and native host/FX lease semantics, not Main per-plugin routing special cases. Verify bypass/reorder/editor lifecycle/state and plugin latency reporting.
- Multi-device work in #763 remains proof-first and independent: one mixer **render owner** with capture/output-only adapters using bounded SPSC and clock drift correction when supported. A second I/O callback is not a second DSP render engine.
- For public/field FAT, compile with one explicit distribution profile and **inspect the actual installed executable** for mandatory ImmerSIB/SonKu assets/catalog. Research profiles must not be confused with a distribution release.
- Benchmark primary ASIO buffer p99/p99.9 deadline and xrun under identical conditions, plugin PDC, control-to-readback and GUI responsiveness. Never claim improved latency from MVVM by itself.

## 7. Fast rollback and merge procedure (not global coordination)

For any PR, independent developer may continue writing to its own branch immediately. Before merge:
1. Fetch current `main`, relevant source diff/PR heads and exact candidate SHA. A same-file overlap requires a clean *reviewed* merge/rebase, not another thread's permission.
2. Compare only the affected behavior, not every historical PR. Use current accepted checkpoints (one GUI, existing audio). Preserve the newer unique source changes and add focused regression fixtures for the conflict area.
3. Run existing path-scoped quick/GUI/native/FAT checks appropriate for code changed, on current exact PR head. If required check fails, repair the **actual error**; no 'get ownership token' escape hatch.
4. Merge through existing branch protection with expected head SHA where the GitHub interface supports it. Note that GitHub Contents API's **blob SHA protects that file**, not an entire multi-file branch transaction; re-read branch HEAD after each series of writes.
5. Disclose `SOURCE IMPLEMENTED / TESTED / PHYSICAL FAT PENDING / RELEASED` separately. A passing CTest is not physical sound proof and a merged PR is not a public installer.
6. If another PR merged first, **reconcile at the later merge boundary**; do not discard ongoing work or freeze other threads.

**Fast rollback:** revert the offending *feature commit/PR*, preserve `.smix` and previously accepted sound/UI. Prefer a temporary scoped compile-time feature flag only when there is a documented fast recovery and no mandatory FX is accidentally excluded from the distribution.

## 8. Proof the new process is nonblocking

These are review criteria for this **document**, not additional required CI jobs:
- A developer can edit `src/Main.cpp` while another draft also edits it, then resolve Git diff and tests.
- A closed, parked, abandoned or stale PR cannot fail another PR's check merely because it overlaps files/domains.
- Missing `arsonmix-workstream`, owner assignments and PR labels cause no CI denial.
- GitHub API search unavailable -> informational limitation; existing path-scoped functional tests still execute.
- Docs-only PR can merge via existing required CI without Windows compiler or human 'architecture permission'.
- A failure to build, missing ImmerSIB, unsafe output activation or native graph regression **still blocks** due genuine code/product constraints.
- Full MVVM migration is accepted only after one ViewModel's old local transitions are removed and equivalent UI/native tests pass, not after producing another ADR.
- Independent SonKu, ImmerSIB, Guitar FX, WIRE and device work continues during migration and cannot be gated on completion of an architecture epic.

**Anti-deadlock rule:** development freedom is the default; only the precise **merge of unverified or unsafe code** is restricted by normal branch protection. No ownership locks, no PR-overlap status check, no extra CI workflow, no forced shared integrator waiting.
