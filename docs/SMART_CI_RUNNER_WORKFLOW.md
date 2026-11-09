# Smart CI: fast feedback, deliberate expensive qualification

**Binding fast-delivery update (2026-11-09):** [Autonomous Fast Delivery Contract](AUTONOMOUS_FAST_DELIVERY_CONTRACT_20261109.md) is the operative agent behavior. Develop independently while quick CI runs; do not convert the M0 source audit into a separate PR/reviewer gate; one product PR and meaningful revisions; first working ArSon Player MVP gets one exact-SHA `ui + fat` (fat already includes native) for an early testable installer. No repeated Windows/Chromium on each push. Preserve current secure owner-only dispatcher, exact SHA/required tests, release and physical FAT safety. **No changes to Actions workflow code are necessary unless a measured bottleneck is found.**

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
