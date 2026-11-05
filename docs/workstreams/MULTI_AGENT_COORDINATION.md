> **ARCHIVED / SUPERSEDED 2026-11-05 — NOT A BLOCKING DEVELOPMENT POLICY.** ArSonMix now operates with a single product integration owner and one working product PR. Historical roles, file restrictions, PR-collision checks and handoff requirements below are retained only for traceability and **must not block implementation**. Current authority: [AGENTS.md](../../AGENTS.md). Native PCM/GUI quality, exact-SHA Windows regression, approved /preview/ visual parity and physical FAT safety requirements remain in force.

# ArSonMix — Parallel Agent Coordination (binding for new work)

**Control tower:** [issue #397](https://github.com/masarray/arsonmix/issues/397).
**Product authority:** [AGENTS.md](../../AGENTS.md) + ONLY the assembled [approved /preview/](https://masarray.github.io/arsonmix/preview/). Do not replace either with an agent-generated UI or JS audio engine.

## Four workstreams, one file owner

| Lane | Only this lane may edit these production paths | What other lanes provide |
| --- | --- | --- |
| GUI | `site/**`, presentation in `web/**`, `scripts/build_pages.py`, CSS and hardware-button assets | Screenshots, interaction tests, exact immutable approved Pages artifact |
| ENGINE | `engine/**`, audio/DSP/driver `src/**` except native GUI seam, VST3 and callback | Native-produced ChannelId/EdgeId snapshots, PCM tests and performance evidence |
| INTEGRATOR | `src/Main.cpp`, `CMakeLists.txt`, `integration/**`, `web/approved-native*`, native GUI resource builder, workflow/coordination policy | Reconciles accepted GUI and ENGINE ABI; no second GUI/PCM; owns merged Windows assembly |
| FAT | Native Windows package/verification scripts, hardware and QA regression evidence | Independent end-to-end exact-SHA verdict; never rewrites DSP or GUI for PASS |

Release-admission scripts (`scripts/check_console_puzzle.mjs`, `scripts/check_approved_native_gui_release.mjs`, `scripts/check_pr278_console_authority.mjs`) belong ONLY to INTEGRATOR. Independent hardware-evidence scripts (`scripts/collect_hardware_fat_windows.ps1`, `scripts/console_v6_fat_gate.mjs`, `scripts/run_console_native_s0.mjs`, `scripts/verify_console_s0_native_fixture.mjs`) belong ONLY to FAT. Neither workstream can weaken the other's release gate without a targeted handoff.

Unclassified docs and isolated tests can be authored by their domain owner. A change to a source file outside a lane **must be handed to the owning lane**. Example: ENGINE can propose a diff to `src/Main.cpp` but INTEGRATOR applies it with the accepted native ABI. FAT files cannot silently turn off safety checks. "Working on a PR" does not grant a file lock against GitHub account credentials: this is enforceable-by-CI collaboration policy, not a security boundary.

## Before every coding session (GitHub is the live coordination bus)

1. Fetch current `main` SHA, root `AGENTS.md`, issue #397, and the active PR list with changed filenames. Check whether another PR or `main` has already implemented your milestone.
2. Read your own workstream and the accepted contract. If your desired file is owned by another lane, **write a targeted handoff**, not another implementation.
3. Reconcile stale branch state against `main`, preserve unique tests only, and keep preview artwork byte-identical. Never blind-merge old #266/#270/#393/#395 branches.
4. Put these metadata markers in each **new** PR body (replace OWNER and issue number):

```text
<!-- arsonmix-workstream: GUI -->
<!-- arsonmix-issue: #397 -->
```

Allowed owners: GUI, ENGINE, INTEGRATOR, FAT. Use the technical issue number when more specific than #397. A temporarily frozen legacy PR may carry `<!-- arsonmix-coordination: parked -->`; remove it only after a fresh ownership + conflict audit.

## Low-cost CI guard (no extra workflow)

`scripts/check_workstream_coordination.mjs` is executed at the start of both **existing** Windows and Chromium workflows on PRs, using GitHub's read-only PR/files API. For PRs created **from #400 onward**, it fails on missing metadata, wrong-owner edits, and changed-file or same-runtime-domain overlap with another active PR that owns the same PCM, bridge or GUI path (even if the filenames differ).

A pre-policy **DRAFT** PR (up to #399) gets **nonblocking findings** to avoid abruptly halting the current ENGINE/FAT branches. Promotion to **ready_for_review** triggers both existing CI paths again and is STRICT, including owner metadata and active-file conflicts. Do not confuse the grandfathering with merge approval: #266 remains the ENGINE producer; stale or overlapping PRs are reconciled and reviewed, not blindly merged. Parked PRs do not claim a file. The current owner can continue when an older branch edits that owner's file unlawfully; the old branch is flagged for handoff instead.

This preflight is **fast and read-only**, runs before JUCE checkout and before browser dependencies, and adds no Windows work for GUI-only changes. If GitHub API cannot be checked, a new PR fails rather than receiving a fictional PASS. Use `node --test tests/workstream_coordination_contract.test.mjs` locally for pure offline regression.

## Handoff format (comment on #397 plus relevant technical PR)

```text
WORKSTREAM: GUI | ENGINE | INTEGRATOR | FAT
BASE main SHA:
PR / head SHA:
OWNED FILES CHANGED:
NATIVE ABI / fixture / ChannelId + EdgeId implications:
EXACT-HEAD PASS (URLs, not assumptions):
BLOCKER / owner needed:
NEXT SINGLE MEANINGFUL MILESTONE:
```

Do not create 10 micro-PRs or issue-only progress in place of code. The owning lane implements substantive code and targeted tests; one integrator joins exact checked SHA. GUI remains Pages-only visual authority; only ENGINE provides real PCM; FAT is the independent final judge. If the device is unavailable, mark physical FAT pending. Keep #15 crackling stop-ship.

## M2.6: always-on lightweight PR coordination

The existing `.github/workflows/ui-smoke.yml` now checks **every pull request**, including docs-only, ENGINE-only and metadata-edited PRs. Its named `PR coordination admission` job checks owner markers, active PR overlaps and runtime domains and runs offline unit regressions. It sets `run_ui=true` only for UI/browser/approved-native changes; docs-only and ENGINE-only PRs do **not** start the heavyweight Chromium job. Windows native CI keeps its existing path filters. No fifth workflow or audio engine is added.

For merge safety, require the exact GitHub Actions job check **PR coordination admission** in `main` branch protection or rulesets, require PR review, and require up-to-date head (or merge queue). Verify a real check run exists before selecting the status name. Do not universally require Windows/Chromium checks when their path-based jobs are intentionally skipped. Configure this in repository Settings → Branches or Settings → Rules → Rulesets using administrator access. Confirm the GitHub branch API reports `protected: true` afterward.

**Admin limitation:** the connected GitHub app cannot write protection/rulesets. Until enabled, a successful CI job is a meaningful PR signal, but it cannot stop an authorized maintainer from bypassing review or pushing directly to `main`. Hardware FAT #15 and exact-sha production gates remain independent and mandatory.

## P0 product qualification — conditional native CI with one required verdict

**Owner:** INTEGRATOR, control tower #397. No second native producer, GUI renderer, DSP callback or workflow file.

The existing `.github/workflows/ui-smoke.yml` is the sole PR entry point. Its always-on `PR coordination admission` computes **`run_ui`** and **`run_native`** from the GitHub API's exact PR changed filenames. It invokes the existing `.github/workflows/p1-windows.yml` as a **reusable workflow** only when native source/ABI, Windows packaging, or release-admission files changed. The Windows workflow still independently builds and packages on `main` pushes/manual dispatch; it no longer starts a **duplicate** path-filtered PR Windows run.

A separate tiny **`Product Qualification`** fan-in runs for every PR, even when its dependencies fail or are skipped. It succeeds **only** if ownership admission passes and every selected lane completes successfully: required Chromium frontend for UI changes; required Windows JUCE/CTest for native changes; both for mixed bridge changes. Docs-only PRs require only admission. A cancelled/failing/skipped **required** lane is a failure, never a fake PASS. Exact-head checks are attached to the PR by the caller workflow; source checkout and ABI ownership are unchanged.

Native classification covers `integration/**/*.h/.hpp/.cpp` (including M3 `ApprovedNative*` headers) and release-admission scripts; isolated `integration/*.mjs` and documentation are **not** blanket triggers. The existing Windows `main` push path filters also include newly covered C++ headers and release gate scripts. The Windows CRLF/LF checkout discrepancy in M2.6 tests is normalized before parsing workflow triggers.

**Administrator action after a green PR:** add the exact GitHub Actions check **`Product Qualification`** to [Protected Main ruleset](https://github.com/masarray/arsonmix/rules/26568033) alongside the existing `PR coordination admission` check, keeping up-to-date branches enabled. The connector cannot alter repository rulesets. Until this is done, the new verdict is visible and functional but not formally a required merge check.

## Administrative limitation

GitHub may allow a maintainer to merge even with a failed check unless **required status checks** and appropriate branch protection/rulesets are configured. App connections cannot always administer these. Treat #397 as the human coordination gate, and enable branch protection for existing Windows/Chromium checks (or a dedicated required coordination check if introduced later) through repository Settings when available. This policy does not magically observe unpushed private ChatGPT threads.
