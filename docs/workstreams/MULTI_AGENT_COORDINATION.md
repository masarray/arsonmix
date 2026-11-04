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

## Administrative limitation

GitHub may allow a maintainer to merge even with a failed check unless **required status checks** and appropriate branch protection/rulesets are configured. App connections cannot always administer these. Treat #397 as the human coordination gate, and enable branch protection for existing Windows/Chromium checks (or a dedicated required coordination check if introduced later) through repository Settings when available. This policy does not magically observe unpushed private ChatGPT threads.
