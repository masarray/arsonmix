# Smart CI: fast feedback, deliberate expensive qualification

All development PRs use native GitHub path filters. This policy is part of the
workflow files and does not rely on a multi-agent coordinator, duplicate
Windows callers, or status-faking.

| Change / event | Automatic check | Expensive worker |
| --- | --- | --- |
| Draft PR, GUI or native bridge | Fast Linux UI contracts | None |
| Draft PR, C++/device/native | Fast Linux native contracts | None |
| Ready-for-review PR, relevant GUI | Fast UI + full Chromium | Chromium |
| Ready-for-review PR, relevant native | Fast native + MSVC/CTest | Windows |
| Non-draft PR new commit | Recheck current HEAD | Relevant full worker |
| main push | Relevant full worker + guarded Pages when UI | Only relevant |
| Manual Actions dispatch | Full selected workflow | Explicit, never auto-canceled |
| Windows dispatch with `package_fat: true` | MSVC + CTest, ZIP creation, FactoryFX staging, ZIP verification + artifact | Windows |

**Operator procedure:** Keep active high-frequency work in *Draft*. Review the
Linux quick results on each relevant change. On a meaningful verified milestone,
use *Ready for review* to run applicable full qualification, or go to
**Actions → Windows Native VST3 Host → Run workflow**, select the exact
branch and check **package_fat** to get an **UNQUALIFIED** Windows hardware FAT
ZIP. **Actions → UI smoke (Chromium) → Run workflow** performs full browser
and historical visual regression on demand.

Manual FAT uses the exact selected commit and is insulated from later PR
synchronizations. The package is not a stable release and cannot substitute
for physical audio-interface listening, ASIO timing or glitch measurements.
Draft checks deliberately do not certify a release. A PR's latest commit must
pass the applicable full checks before merge.

The runner economy comes from *not scheduling* paid Windows or Playwright
during draft iteration, not from sleeps, fake success, silenced CTest, or
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
