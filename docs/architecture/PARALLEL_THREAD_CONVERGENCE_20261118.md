# ArSonMix — Parallel Thread / Multi-Agent Convergence Protocol (2026-11-18)

**Status:** PROPOSED CURRENT-POLICY RECONCILIATION; not a new CI gate, not proof of merged implementation.  
**Source audit:** `main@ef7a29933109020657accfb957b7a6ffc8d58cd7`, 2026-11-18.  
**Parent:** [#527](https://github.com/masarray/arsonmix/issues/527); [Modular MVVM ADR](ADR_MODULAR_MVVM_REALTIME_20261118.md).  
**Higher authority:** [AGENTS.md](../../AGENTS.md), approved `/preview/`, active ruleset, accepted source/physical hardware evidence.

## 0. Problem and a source-confirmed policy contradiction

The historical [four-lane guide](../workstreams/MULTI_AGENT_COORDINATION.md) is already marked **SUPERSEDED** by `AGENTS.md` single product integration owner. Yet the current `.github/PULL_REQUEST_TEMPLATE.md` still demands old `GUI / ENGINE / INTEGRATOR / FAT` ownership markers, and `scripts/check_workstream_coordination.mjs` retains historical lane admission logic and a special single-owner bypass. Tests still encode both regimes.

**Decision:** one product-feature integration PR may edit all necessary layers, but parallel agents contribute **bounded domain implementations** with explicit contracts and a single integrator responsible for final consistency. There is no extra human approval committee, fixed four-lane handoff barrier, or mandatory sequential coding. Do NOT remove existing security, physical-safety, release, or CI gates. Old draft PRs are NOT automatically current work just because still open.

## 1. Why previous parallel work becomes chaotic

A title, previous ChatGPT discussion, PR base SHA, old plan or green quick test is not sufficient authority. Concurrent agents may:
- implement two versions of the same routing/graph command in distinct files;
- copy old `Main.cpp`, CSS or native bridge and overwrite recent accepted work;
- mistake logical `.smix` ACK for PCM-installed or physically audible proof;
- build/install a valid EXE with a different compiled FX catalog (ImmerSIB incident);
- each create a mini-PR that cannot stand alone or merge old stacked research;
- each modify broad shared build/CI files while believing the files are “theirs”.

The prevention mechanism is **active source reconciliation + contracts + bounded integration + actual enforcement**; not more general promises.

## 2. One live source-of-truth chain

```text
OWNER-APPROVED USER OUTCOME / golden checkpoint
  -> one current GitHub issue/acceptance contract
  -> LIVE main commit SHA + open in-scope PRs, exact PR HEADs, changed paths
  -> domain/contract overlap assessment and integration owner
  -> one coherent product integration PR (may receive reviewed independent commits)
  -> exact-head appropriate tests + required protected-main gate
  -> merge verified head -> main (never blind old branch merge)
  -> candidate build (compiled catalog + provenance)
  -> owner operator/physical FAT (when required) -> release
```

**Precedence:** owner product constraints and live accepted behavior > current main implementation/contract > latest actively verified integration PR for a not-yet-merged capability > bounded candidate work > historical PR docs. A dated README or PR body is never proof a capability is present in the currently installed binary. Verify field state by actual exact-artifact evidence.

## 3. Mandatory preflight for EVERY thread/agent (no extra paid CI)

1. Retrieve default branch HEAD SHA and the exact relevant `AGENTS.md`/architecture contract.
2. Discover all **open in-scope** PRs, including draft, plus their HEAD SHA, changed paths, merge base, tests/artifacts and whether current main already contains the requested work. Check `#527` for platform seams and feature-specific issue/PR for golden acceptance. Do not rely on one previous chat handoff.
3. Assign a **unique target outcome** and module boundary: what gesture/audio behavior becomes possible, which existing source must remain identical, and what is explicitly NOT in scope.
4. Build a **conflict map**: same file, same runtime domain, shared contract/ABI, shared release flags, and shared user-facing GUI. Only explicit no-conflict cases are safely parallel.
5. Select one of: extend the active feature PR with non-destructive expected-head reconciliation; independently develop an additive module + tests against a stable versioned contract; or defer source integration until conflicting PR is reconciled. One product milestone / one integration owner.
6. For any GitHub write verify current target branch HEAD and expected SHA. Do not force-push/overwrite another agent's branch or blindly cherry-pick whole legacy stacks. Refetch on conflict.
7. Record exact source HEAD, integration status, tests run, tested artifact SHA, pending physical evidence and next dependency in the PR. If no tests were run, say so.

**Do not issue a generic agent “lock” that blocks unrelated work**. Issue/PR claims identify intent, not an enforceable file lease. GitHub expected-SHA writes and protected-main checks supply actual mutation safety.

## 4. Conflict matrix: decision before coding

| Concurrent changes | Classification | Required action |
| --- | --- | --- |
| Different self-contained FX DSP files; shared host API unchanged | GREEN | Work in parallel; each tests DSP, then one integration PR checks host semantics |
| GUI cosmetic changes vs PCM fix, exact approved renderer/bridge API unchanged | GREEN/AMBER | Parallel with frozen visual/ABI contract, integrated UI/native snapshot verification |
| Different files but same `MixerSessionDocument` / `ChannelId` ABI or graph publisher | RED | Agree one versioned interface first; one integration owner serializes boundary commit |
| Two PRs editing `src/Main.cpp`, `approved-native-bridge.js`, `CMakeLists.txt`, shared routing, or same accepted GUI | RED | Source-level reconciliation against live main; combine unique hunks, preserve both behaviors; never wholesale replace |
| Historical draft overlaps newer released SonKu/ImmerSIB/Gain checkpoint | RED | Park/archive old proposal; forward-port only proven unique hunks after test; do NOT bulk merge |
| Two PRs touch distribution profile, compulsory FX catalog or installer | RED | One manifest/product owner; prove compiled catalog parity and artifact provenance |
| Docs-only independent architecture research | GREEN | Proceed on separate branch, cheap review/gate, no Windows/FAT claim |
| Shared realtime callback or vendor FX editor lease despite distinct filenames | RED | Single runtime owner integration and exact-head native regression; no parallel merge |

“Same file” is a heuristic, NOT the limit of collision detection. A feature has one runtime domain even when implemented across different files. No automatic silent merge of RED changes.

## 5. PR lifecycle and minimum machine-readable handoff

Use GitHub PR body as the discoverable, editable claim; prefer linked issue and actual diff to a chat-only handoff. Adopt this template for **new integrated product PRs** (documented here, not a schema already enforced):

```yaml
workstream:
  mode: single-product-integration
  issue: 527
  productArea: mixer-routing
  ownerPR: 0                  # replace with actual PR number
  basedOnMain: "<40-char SHA>"
  currentHead: "<40-char SHA>"
  acceptedCheckpoint: "<existing tag/SHA or none>"
  changedDomains: [routing, native-bridge]
  dependencies: [ ]           # PR # or frozen API contract
  conflictsReviewed: [ ]      # overlapping PR # with resolution
  releaseProfile: none        # public-alpha / field-fat / research / none
  sourceTests: pending        # exact run/commit after run
  guiFAT: pending
  nativeFAT: pending
  physicalFAT: pending
  release: not-requested
```

`ownerPR: 0` means not assigned yet, never proof of ownership. This YAML is advisory until its parser/checks are deliberately added; **do not claim bots enforce it**. The current accepted integration marker `<!-- arsonmix-integration: single-owner -->` remains a compatibility marker for established CI behavior and must not be misused to bypass safety checks. Legacy `<!-- arsonmix-workstream: ... -->` is historical, not required for new product direction.

**Handoff states are distinct:** REQUESTED -> SOURCE_IMPLEMENTED -> EXACT_HEAD_TESTED -> PRODUCT_REVIEWED -> OPERATOR_FAT (if applicable) -> PUBLISHED. A state may be skipped only when nonapplicable and explicitly explained. Draft PR ≠ product ready; CI pass ≠ physical ASIO FAT; merged ≠ downloaded user release.

## 6. Controlled integration algorithm (the one agent holding the integration PR)

**Pre-integration:**
1. Pin current main HEAD, source candidate HEAD, feature area, accepted checkpoint and all overlap PR HEADs.
2. Compute merge-base + per-file diffs and detect changed-domain/contract overlap, not just Git auto-conflicts. For a high-risk file, compare **current main + old branch + candidate** semantics, including tests and binary resources.
3. Select authoritative newer behavior and only forward-port **unique, proven** changes. Preserve accepted SonKu Venue sound, ImmerSIB owner-approved tuning, Gain GUI, actual VST3 editor/lease state, `.smix`, routing and WIRE. Avoid late changes outside the user milestone.
4. Require one coherent result: behavior, UI intent/native ACK, DSP/graph, persistence and tests work end-to-end. No false state projection.
5. Update source branch using expected SHA lease / clean reviewed commit. Re-run relevant fast tests; dispatch one expensive exact-SHA UI and/or native/FAT lane as needed. FAT includes native: do not duplicate paid native run.
6. Wait for required merge status to pass on **that exact PR HEAD**. Recheck main/PR HEAD just before merge; if moved, reconcile and rerun affected tests. No forced auto-merge of stale state.
7. After merge read actual main SHA and release manifest; only then update docs, other issue statuses and handoff; no implicit release.

**Branch management:** short-lived integration branches, no long-lived parallel integration stacks. Research branches may remain archived with exact SHA but cannot silently become runtime authority.

## 7. CI enforcement (what exists vs what is proposed)

**Observed 2026-11-18:** protected `main` ruleset `26568033` has required context `ArSonMix / Required CI Merge Gate` configured. This updates the earlier 2026-11-17 gap; inspect current ruleset and failing/red PR behavior before claiming enforcement tested. Existing `required-pr-merge-gate.yml` is a trusted status publisher and respects path-scoped qualification.

**Next rollout, not done by this document:**
- Replace superseded four-lane template/help text and obsolete blocking assumptions without weakening required checks.
- Build a *read-only* PR contract impact reporter: changed domains, open overlap PRs, base/head SHA, missing feature evidence; WARNING for independent work, BLOCK only a verifiable high-risk invariant (e.g. alternate GUI, duplicate callback, missing compulsory FX, invalid stale head). API unavailability must be explicit, not fabricated “no overlap”.
- Add domain contract fitness checks to the **existing** fast native/UI workflows. Prevent new UI -> DSP includes, duplicate ownership, session schema mismatch, and release profile drift. Source import/include graph tests must be scoped to actual new modules, not fail on legacy debt en masse.
- Use GitHub CODEOWNERS for review notification where justified, but it requires maintained owners and required-review configuration to enforce; **do not add fake team names**.
- Consider merge queue **only after** required workflows support `merge_group` event; otherwise enabling merge queue can deadlock the repo. See GitHub documentation.
- Preserve fast docs-only PRs, one deliberate costly Windows FAT per candidate, reproducible source SHAs and verified artifact catalog.

## 8. Acceptance tests for governance itself

- New PR with no changed runtime contracts can proceed without unrelated owner lane approval.
- Two independent plugin DSP changes can progress concurrently; conflict reporter does NOT falsely block them.
- Two PRs editing separate files of the SAME graph/ABI are flagged RED.
- A legacy branch cannot replace approved GUI/accepted DSP by automatic merge.
- A PR with a stale HEAD or failed required check cannot be merged under configured rule.
- A release/FAT built without compulsory ImmerSIB catalog or scene assets fails before delivery.
- A docs-only PR never invokes an unnecessary full Windows build.
- A new agent reading `AGENTS.md` -> current architecture -> this protocol can find the current domain owner, supported extension seam, and exact tests without resurrecting old workstreams.
- A finished PR handoff states explicitly what is code-verified, operator/physical-FAT verified, published, and still pending.

These are **target acceptance criteria**, not a claim all are already implemented or observed.

## 9. Immediate execution order

1. Merge a reconciled template and source-of-truth architecture index after review (this research workpack).
2. Retire remaining hard four-lane CI/path assumptions *in one focused implementation PR*, not by deleting safety tests; add conflict classification cases first.
3. Pilot MVVM on one routing/WIRE UI slice without changing approved look or native PCM.
4. Extract one native control use case and integrate generic FX extension tests.
5. Gradually retire M4 paths only with real rendered-PCM/rollback evidence.
6. Add cross-module architecture fitness + compiled artifact feature parity checks.
7. Measure reduction in PR conflict rate, unrelated files touched per feature, rework, regression escape, p99 audio deadline and control-to-readback latency against a recorded baseline.

**Final rule:** no thread writes or merges to an active shared source surface until it has checked current main, scoped PRs, accepted interfaces and reviewed diff. Do independent bounded work immediately; serialize only the actual conflicting boundary, not the entire product.
