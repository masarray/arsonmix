# Smart CI: fast feedback, deliberate expensive qualification

**Binding fast-delivery update (2026-11-09):** [Autonomous Fast Delivery Contract](AUTONOMOUS_FAST_DELIVERY_CONTRACT_20261109.md) is the operative agent behavior. Develop independently while quick CI runs; do not convert the M0 source audit into a separate PR/reviewer gate; one product PR and meaningful revisions; first working ArSon Player MVP gets one exact-SHA `ui + fat` (fat already includes native) for an early testable installer. No repeated Windows/Chromium on each push. Preserve current secure owner-only dispatcher, exact SHA/required tests, release and physical FAT safety. **No changes to Actions workflow code are necessary unless a measured bottleneck is found.**


## 2026-11-12: single paid Windows milestone and fail-closed artifact economy

**Incident:** two owner-dispatched full MSVC/CTest Windows jobs (one `native`, one `fat`) overlapped on ArSonMix PR #579. Both rebuilt the complete JUCE application from scratch, and the FAT job then re-compressed already-compressed Inno/ZIP archives for GitHub upload. This cost time without independent qualification value. Source: [native run 43093641494](https://github.com/masarray/arsonmix/actions/runs/43093641494) and [FAT run 43094957798](https://github.com/masarray/arsonmix/actions/runs/43094957798).

**Binding practice / no redundant Windows runner:**

1. For any feature that requires an installer or field-test executable, send **`fat` only**, never `native` followed by `fat`. `fat` includes **full Windows MSVC, full CTest, source/artifact exact-SHA verification, official VB-CABLE verification, Inno installation/upgrade and upload**. GUI requires its own one-time `ui` visual gate where appropriate.
2. For device/DSP fixes **without** any consumer artifact requirement, send **`native` only**, using the same full native tests but without installer packaging. Draft/PR pushes always get cheap Linux source-contract CI first.
3. All Windows owner comments dispatch the **reviewed main workflow** with `inputs.expected_sha` set to the authentic PR head and `inputs.request_pr_number` set by the owner-verified dispatcher. The checkout/verifier uses expected source SHA, **not** main's `github.sha`. The public run title contains `ArSonMix FAT/NATIVE PR# SHA` to distinguish two independent runs from identical workflow-ref SHA.
4. The dispatcher deduplicates one native/FAT lane per exact PR+source SHA. If a FAT is active for the PR, no other expensive Windows lane may start. If a FAT is requested while native is still active for **that same verified PR**, the dispatcher cancels only the obsolete native build through the GitHub Actions API before dispatching FAT. It never cancels a FAT or another PR's worker.
5. FAT/manual runs are immutable even if a PR receives new commits; a new source SHA requires new qualification and is not mislabeled as the old artifact. Native-only runs of the same PR may supersede older native runs. Failed same-SHA FAT is **not automatically rebuilt**; repair code on a new commit or use GitHub's *failed-jobs retry* after diagnosing a transient external failure.
6. Installer EXE, Inno setup and FAT ZIP are already compressed. Upload with `compression-level: 0`; retain complete artifact bytes, manifest, SHA256 checks and 14-day artifact retention. This is a packaging overhead optimization, **not** a skipped verification step. No expensive Windows worker runs on a prose-only change.

**Exact FAT upload failure recovery (observed incident):** [run 43094957798](https://github.com/masarray/arsonmix/actions/runs/43094957798) built the real JUCE app and passed **92/92 CTests**, ZIP verification, Inno install/upgrade and genuine historical-release rollback. It failed only at `actions/upload-artifact@v4`: the GitHub Artifacts API returned an HTML document instead of JSON five times (approximately 11 minutes of backoff). No ZIP was stored. That is an upstream artifact service failure; rebuilding the app cannot repair it.

The optimized FAT workflow now ends the full native Windows job by sealing a SHA256 receipt for **every staged file**, saving `dist/`, `installer-output/` and `delivery-receipt.json` under a run-unique immutable Actions cache key. A separate `fat-upload` job restores it with **no fallback keys**, checks the checked-out source SHA, full file list, hashes, native EXE manifest and Inno setup existence, then publishes both GitHub Artifacts. A transient upload failure now fails only `fat-upload`; use the connector's **re-run one failed job** action on that job ID to retry distribution, not `native` or `fat` on a new run. Cache expiry or corruption is **fail-closed**, never an excuse to publish unsigned or unchecked bits. Do not claim that the previous failed run's files remain recoverable: the mechanism applies only to builds made with the new workflow.

**CI-only main merge gate:** The main `push` workflow still starts and runs all cheap native contracts. Before scheduling Windows, it checks `github.event.before..HEAD` on a strict allowlist of five CI dispatcher/workflow/test/runbook files. Only a complete diff entirely within those files may skip the ~30-minute MSVC rebuild. Any app/runtime/CMake/audio/installer file, incomplete history, unknown path, or deliberate `workflow_dispatch` always compiles and runs full CTest. This ensures that merging this efficiency PR does not trigger the very redundant Windows build it is designed to prevent.

**UI/Pages cost and safety:** Full Chromium on main should be triggered by a real `web/**`, `site/preview-v2/**`, preview assembly or GUI regression source change—not a dispatcher/test/workflow-only maintenance commit. Keep these CI-only paths in `ui-smoke.yml` **pull_request** filters for fast review, but NOT in its **push** filters. Do not simply mark full Chromium `skipped` after quick UI success: the downstream Pages `workflow_run` publisher could interpret that fast-only success as visual qualification and republish unqualified GUI. By having no UI push event for CI-only changes, Pages receives no false success. Real GUI commits still execute the exact full Chromium/CSS ownership gate; CSS overlap failures remain visible and must be fixed in the GUI integration PR. This follows the observed CSS ownership failure from [CI-only main push run 43101744603](https://github.com/masarray/arsonmix/actions/runs/43101744603), which changed no CSS but nevertheless ran costly full browser qualification on a pre-existing issue.

**Source of truth for gate:** `scripts/chat_ci_dispatch.mjs` and `tests/chat_ci_dispatch_contract.test.mjs` must pass together. `p1-windows.yml` retains `Build executable and native RT tests`, `Test realtime control primitives`, `verify_native_windows_artifact.ps1`, `build_verify_windows_installer.ps1` and two real artifact uploads. Do not claim field ASIO listening or public RC release on CI evidence alone.

**Backward compatibility:** Run names from workflows predating this rule may not contain the verified PR number and checkout SHA. They are *not* treated as evidence for another source commit. Do not re-dispatch the still-active legacy run while it is producing an exact-SHA FAT ZIP; inspect its actual run and artifact status first.

## Mandatory start for a new ChatGPT / Codex thread

1. Open [root AGENTS.md](../AGENTS.md) and this runbook from **current
   `main`**. Inspect `.github/workflows/chat-ci-milestone.yml`,
   `scripts/chat_ci_dispatch.mjs`, `ui-smoke.yml` and `p1-windows.yml`
   before modifying CI.
2. Query GitHub via the connected connector: live main SHA, active PRs and
   current PR HEAD, latest commits/changed files, quick CI, manual milestone
   runs and artifacts. Resume the **existing** integration milestone if it
   covers the request; do not fork a second PCM/GUI authority or open a
   redundant PR. A previous chat or a snapshot SHA is not source authority.
3. Implement one significant, reviewable **vertical product increment** in one
   coherent PR with deterministic tests. An M0 architecture/PCM trace is part
   of M1 end-to-end implementation, **not a separate signoff PR**. Preserve
   `/preview/`, native ACK/readback, realtime callback, and VST3 state. Push
   only useful revisions; small edits do **not** justify heavyweight jobs.
4. The agent may continue independent coding while fast CI is pending; it does
   **not** wait for a full Windows/Chromium check to continue other tasks. Fix
   relevant CI failures before milestone dispatch/merge. The agent owns the
   GitHub workflow end to end **in the active chat**:
   branch/commit or PR update, fast-failure investigation, selective exact-SHA
   milestone PR comment, downstream job/artifact inspection, and merge when
   evidence is sufficient. Do not ask the user to open GitHub Actions to
   manually trigger a run. If connector permissions are missing, report it.
5. Before finishing or handing off, record the real `main`/PR HEAD SHAs,
   files changed, which checks passed, which expensive lane actually ran or
   was skipped, whether the ZIP is still UNQUALIFIED, blockers and next
   meaningful vertical milestone. **Never imply future background execution.**

**Historical instructions:** `docs/workstreams/MULTI_AGENT_COORDINATION.md`
was superseded by single-owner integration; old multi-agent admissions,
required check names, four-lane handoffs and manual Actions instructions do
not override this policy.

## Cost-aware milestone decision table

| Actual change / evidence required | ChatGPT decision |
| --- | --- |
| Docs, agent guidance, prose-only | No Windows/Chromium dispatch; inspect diff and links |
| Normal in-progress PR commits | Only relevant fast Linux check; repair failures before expensive CI |
| Visual GUI, CSS, responsive, WIRE mouse behavior | One `ui` request after quick checks; inspect browser evidence |
| C++, VST3 host, DSP, device, ASIO, IPC/control | One `native` request after quick checks; inspect MSVC + CTest |
| Physical hardware FAT requires an executable | One `fat` request; MSVC + CTest + verified unqualified ZIP |
| Mixed GUI and native product milestone | `ui` plus either `native` or `fat`, only if needed and not already qualified |
| Player MVP requiring a real user-test ZIP | One `ui` plus one `fat` at the accepted exact SHA, after fast UI/native; **never** duplicate with `native` for that SHA |
| Existing same-SHA success/running request | Reuse its result; do not post another dispatch comment |
| Any relevant `main` promotion | Normal `main` safety CI / guarded Pages deployment, never skipped to save time |


All development PRs use native GitHub path filters. This policy is part of the
workflow files and does not rely on a multi-agent coordinator, duplicate
Windows callers, or status-faking.

| Change / event | Automatic check | Expensive worker |
| --- | --- | --- |
| Draft PR, GUI or native bridge | Fast Linux UI contracts | None |
| Draft PR, C++/device/native | Fast Linux native contracts | None |
| Any PR review state, relevant GUI | Fast UI contracts | None until assistant milestone |
| Any PR review state, relevant native | Fast native contracts | None until assistant milestone |
| Any PR new commit | Recheck current HEAD | None until assistant milestone |
| main push | Relevant full worker + guarded Pages when UI | Only relevant |
| ChatGPT GitHub comment on a meaningful milestone | Full selected workflow | Only chosen lane, never auto-canceled |
| Windows dispatch with `package_fat: true` | MSVC + CTest, ZIP creation, FactoryFX staging, ZIP verification + artifact | Windows |

**Workflow:** Every relevant PR commit receives Linux fast contracts regardless
of Draft/Ready. When the assistant reaches a meaningful milestone, it obtains
the exact current PR HEAD, confirms applicable fast checks and absence of
a redundant same-SHA run, then posts one owner-authorized `/arsonmix-ci`
command through the GitHub connector. The dispatcher chooses only the necessary
Windows/Chromium lane and can package a verified **UNQUALIFIED** Windows FAT
ZIP. The user never has to press **Run workflow**.

Connector-dispatched FAT uses the exact selected commit and is insulated from
later PR synchronizations. The package is not a stable release and cannot substitute
for physical audio-interface listening, ASIO timing or glitch measurements.
Draft checks deliberately do not certify a release. A PR's latest commit must
pass the applicable full checks before merge.

The runner economy comes from *not scheduling* paid Windows or Playwright
during PR iteration, not from sleeps, fake success, silenced CTest, or
removing critical audio safety tests. Path-only changes to docs do not
start UI/native workflows. The main Pages publisher remains gated on the
successful full GUI push and checks the exact current main SHA.


## Autonomous ChatGPT GitHub connector CI trigger

You do not need to open the GitHub Actions page. ChatGPT posts one
owner-authenticated **PR Conversation** comment using the connected GitHub app:

```text
/arsonmix-ci ui <exact 40-character PR HEAD SHA>
/arsonmix-ci native <exact 40-character PR HEAD SHA>
/arsonmix-ci fat <exact 40-character PR HEAD SHA>
```

The main-branch `ChatGPT CI Milestone Dispatcher` verifies that the comment
comes from the repository owner, the PR is open and belongs to this repository,
the SHA is its actual current HEAD, and fast checks have not already failed.
Requests for the same lane+SHA are deduplicated; previously qualified or
running manual UI/native dispatches are not restarted. The assistant picks
the required lane only for a meaningful milestone, not for every commit.

`ui` requests full Chromium interaction/visual tests; `native` requests
Windows MSVC and realtime CTest; `fat` requests Windows MSVC/CTest followed
by independently verified **UNQUALIFIED** Windows hardware FAT ZIP.
The dispatched workflow checks out the exact authorized source SHA. PR
comments cannot supply arbitrary workflows or secrets.

Older branches created before these `expected_sha` workflow inputs were
introduced must first reconcile the new main CI workflow files. Manual Actions
buttons remain an optional fallback only, not the user-facing process.

## Execution and evidence protocol for agents

The GitHub connector available to ChatGPT does not expose a direct
`workflow_dispatch` call. Use its **add_comment_to_issue** action targeting
the **PR number**, producing a top-level PR Conversation comment whose **entire
body** is one valid command line above. Do not use an issue without a PR,
an inline review comment, a markdown fenced command, a stale SHA, or an
unauthenticated caller. GitHub's owner-only `issue_comment` dispatcher makes
the actual Actions API dispatch. A comment by an agent identity that is not
authenticated as the repository owner cannot pass the dispatcher. Check the
connected GitHub login/permission first if uncertain.

Read the latest PR HEAD **immediately** before sending. Verify fast UI/native
contracts from that HEAD, and look for an earlier owner comment and same-SHA
`workflow_dispatch` run. If already requested, inspect/reuse; do not spam a
second command. If the fast check fails, fix the underlying regression and
push a new revision first. This applies even if the user explicitly wants a
quick milestone: do not consume expensive runners on a known broken source.

Watch the `ChatGPT CI Milestone Dispatcher` result. A successful dispatcher
means only **dispatch accepted**, not that Windows/Chromium was successful.
Follow its downstream run to terminal conclusion and inspect failing steps and
logs; collect artifact ID + build provenance for a successful `fat` run.
Differentiate missing artifact, pending, failed and verified-but-unqualified.
If a later commit moves PR HEAD, never present the previous ZIP as validation
of the new commit. Only the latest correct SHA may be claimed as qualified.

For a docs/instructions-only PR like this policy handoff, GitHub file path
filters intentionally do not allocate costly runners; review the diff and
check that no workflow/runtime files were changed. Merge documentation PRs
only after branch/main state is reconciled; preserve unrelated active product
PRs. A successful PR documentation merge does not prove physical audio, VST3
or hardware reliability.

### New-thread completion format

```text
Repository: masarray/arsonmix
Current main: <SHA>
Product PR / branch / exact HEAD: <PR, branch, SHA; or none>
Meaningful work performed: <files + behavior, not number of microcommits>
Fast checks: <run links + success/failure, or not applicable for docs-only>
Milestone lane: <ui | native | fat | none> / reason / dispatcher + run URLs
Windows ZIP: <artifact and provenance OR no artifact / FAT PENDING>
Physical audio FAT: <verified on equipment OR PENDING>
Unresolved risk and next significant milestone: <specific>
```


## Historical CSS migrations

Full current-product UI qualification keeps archived V8–V27 migration tests disabled unless `historical_css: true` is explicitly requested. Current approved GUI, native interaction, browser and provenance checks remain mandatory.

## Operator-first fast delivery amendment (2026-11-09)

Read [Autonomous Fast Delivery Contract](AUTONOMOUS_FAST_DELIVERY_CONTRACT_20261109.md). Continue code editing while fast CI runs when tasks are independent; finish one real native Player→MUSIC→MAIN workflow before cosmetic or M2 features. No separate M0 plan/review/merge gate, repeated owner approvals, parallel micro PRs or dummy commits to start expensive Actions. Preserve fail-closed hardware identity, Native/Browser quality, exact-SHA jobs, permissions and branch protection. Trigger early unqualified Windows FAT installer **once** when M1 native audio is integrated, so owner can field-test; any later runtime fix requires a new exact-SHA qualification. Do not claim physical FAT on CI success.

### Portable Windows field preview

Use `/arsonmix-ci field <exact current PR HEAD SHA>` when the next step is hands-on testing of the current mixer on Windows. The owner dispatcher runs the existing fast native checks, builds only `ARMixConsole`, verifies the exact-source portable package, and uploads `ArSonMix-Windows-Field-Preview-<SHA>`. Download the Actions artifact ZIP, extract it and run the included executable. Use `fat` for installer testing or full Windows qualification. The field ZIP records that full CTest was omitted and carries no public release or physical qualification.
