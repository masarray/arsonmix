# ArSonMix — Autonomous Fast Delivery Contract (GitHub ↔ ChatGPT)
**Binding owner policy · 2026-11-09 · Status: operative development workflow; not a claim of new product code**

**Priority:** ship the next actually usable ArSonMix vertical milestone as rapidly as reliable engineering allows. **Speed = fewer handoffs, fewer duplicate jobs, fewer redundant revisions, less waiting, and more complete user-tested software — not weaker tests.** This document supplements [AGENTS.md](../AGENTS.md) and [Smart CI runbook](SMART_CI_RUNNER_WORKFLOW.md). Product-specific goals for ArSon Player: [Zero-friction workflow](ADR_ARSON_PLAYER_ZERO_FRICTION_SMART_WORKFLOW_20261109.md), [Agent Workpack](workstreams/ARSON_PLAYER_IMPLEMENTATION_WORKPACK_20261109.md).

## 1. Agent operating mode — autonomous, scoped, evidence-driven

1. **One quick live inspection** per working session: fetch main/active relevant PR/HEAD/changed files/current fast checks, then go straight into editing. Do not read 50 unrelated threads or repeatedly survey all CI until needed. Inactive historical PRs and snapshots are not barriers. Honor expected-SHA leases and re-check main before merge.
2. **Do work, not another planning cycle.** Read existing PRD/ARD once, identify the direct PCM/bridge/UI integration seam, implement it and add targeted tests. No new Lab, special permission/reviewer gate, coordinate-only PR, “architecture spike pending signoff”, or a chain of tiny PRs before user hears sound.
3. **Single product owner, single integration PR** per coherent deliverable, covering engine + GUI + bridge + tests + packaging as appropriate. Use short local commits/connector updates under that PR. Avoid stacked dependencies and a new PR for each class, unit test, or UI knob. Reuse an existing in-scope PR when safe; do not overwrite another agent's HEAD.
4. **Build correct by design:** start with stable ChannelId/source contracts and accepted native graph; define callback-safe ownership; keep one JUCE render, one approved `/preview/`, one command/readback authority. Do not route around broken P0 by creating WebAudio, fake pin ordinals or a second audio graph. Performance comes from architecture, not patches.
5. **Agent owns complete GitHub workflow inside this live task:** create/commit/push PR through connector, read CI evidence, fix actual failures, selectively trigger exact-SHA UI or native/FAT with the connected owner PR-comment dispatcher, inspect artifacts and merge if protected conditions allow. Don't delegate routine GitHub operations or repeated “Run workflow” requests to owner.
6. **Never fake progress.** Declare real source, test pass/fail, merge and usable artifact with exact GitHub links; mark physical ASIO FAT PENDING when untested. A completed TODO skeleton or browser demo does not count as the user-visible vertical slice.

## 2. The fast path (normal agent execution)

```text
SESSION: read exact main+active PR -> select one user-observable milestone
    -> implement engine + one fixture + native command/readback + approved GUI
    -> run smallest relevant local/fast check
    -> fix red tests; continue independent coding while CI is queued/running
    -> one integrated PR / push meaningful working revision
    -> inspect quick CI only when necessary for expensive dispatch
    -> select exact-SHA lane:
         docs-only: NONE
         GUI-only: UI
         native-only: NATIVE
         combined GUI+native: UI + (NATIVE or FAT)
         playable Windows artifact requested: UI (if GUI changed) + FAT [FAT includes native]
    -> read terminal evidence and FAT artifact provenance
    -> merge scoped verified PR (if permitted) or leave open with exact blocker
    -> hand off a real tested user workflow, not the number of commits
```

**No stop-the-world effect:** A pending quick or long-running Chromium/MSVC job must **not** suspend unrelated source edits; work on independent tests/GUI/contracts immediately. **Before a costly dispatch** verify latest exact HEAD has applicable quick success; **before merge** verify all mandatory code/GUI gates at the exact source version. Never bypass relevant failures, stale SHA checks, branch protection or hardware safety.

**CI checking cadence:** after push/check event; before exact-SHA milestone dispatch; at actual completion/failure; and before merge. Avoid polling every few seconds and posting identical commands. An in-progress Windows FAT can coexist with code work but its package applies only to the **old immutable SHA**; if the subsequent code changes audio behavior, requalification of the *new candidate* is required.

**Do not introduce artificial approval:** No ChatGPT “do you approve plan?”/reviewer admission/agent-owner wait/prompt for user to click Actions/disabled Play until lab pass. For genuine physical hardware listening, use the packaged FAT as soon as a meaningful vertical works; not every intermediate commit. Explicit user approval is still necessary when changing an unknown physical output; not for routine software graph preparation on trusted hardware.

## 3. CI policy: minimal reliable gates, no repeated heavyweight work

| Change/milestone | Automatic on PR update | Deliberate milestone (exact SHA) |
| --- | --- | --- |
| Documentation only | None (review diff and links) | None |
| C++/PCM/VST3/driver | Path-scoped Linux fast native | **native** MSVC/CTest once if no installable artifact needed; **fat** INSTEAD of native when installer is needed |
| Approved /preview/ GUI/CSS/JS | Path-scoped Linux fast UI | **ui** Chromium screenshot/gesture smoke once |
| Player MVP (native + GUI; executable wanted) | Fast native + fast UI only | **ui + fat**, no redundant `native` run |
| Audio runtime bug after initial FAT | Affected fast checks | One newly relevant exact-SHA `fat` after real fix, not repeated same-SHA reruns |
| Existing successful/running exact-SHA lane | Nothing extra | Reuse results; no duplicate dispatch |
| Relevant main merge/release | Existing main-required full checks | Never suppress release hardening or physical FAT claims |

This is already the design of repository workflows `p1-windows.yml`, `ui-smoke.yml`, and `chat-ci-milestone.yml`: **do not add more CI jobs or modify the release pipeline without evidence of an actual bottleneck.** The owner-authenticated dispatcher is one PR Conversation comment `/arsonmix-ci <ui|native|fat> <exact HEAD SHA>`; agent posts via GitHub connector. `fat` includes MSVC/CTest and Windows ZIP verification. The agent does not ask user to navigate Actions.

**Safe CI optimizations:** path filtering; cancel superseded *quick PR* checks, never cancel user-requested immutable qualification; prepare first scoped failure fixtures before Windows; reuse prior successful same-SHA jobs/artifacts; run focused CTest/JS/Python locally before pushing; skip historical migration suites unless the affected surface warrants them. Avoid multi-OS release matrix during every iteration, but run platform/release qualification when release work actually requires it.

**Forbidden shortcuts:** fake green check, altered assertions to mask regression, removing relevant audio safety/rollback tests, disabling native runtime check on code change, treating current-source failure as “CI noise”, uploading a ZIP before build/verify, treating FAT artifact as physical listening PASS, silently publishing a release, or bypassing protected branch policy. CI optimization must optimize *work*, not weaken *evidence*.

## 4. Product milestones are vertical, not artificial stages

**For ArSon Player**, M0 is an **internal engineering slice in the same ongoing M1 implementation PR**, not a separate “architecture spike PR” or approval gate. Native typed virtual source + player-first-sound planner + deterministic tests feed directly into: file drop/import → prepared decoder PCM → stable MUSIC ChannelId → trusted MAIN WIRE → FX/fader/MAIN → physical output → native ACK → real approved GUI.

**First user-test artifact target:** as soon as that end-to-end flow works, run fast checks, one `ui` if GUI changed, one `fat` for an actual Windows playable package. Do not wait for advanced marker/crossfade/stems/tempo analyzer to produce the first installer; do not ship unimplemented KEY/TEMPO UI as fake controls. Real pitch/tempo DSP may be M2 on the same product branch or subsequent small tested milestone, not a reason to postpone usable M1.

**M2/M3/M4:** add independent pitch/tempo, setlist live-safety and eventually advanced dual decks; keep the initial simple workflow intact and avoid cross-thread workstream gates. Only branch for separate coherent work when actually necessary; do not mix alpha release candidate or unrelated Harmony/Vocal Throw work.

## 5. Optimized engineering quality rules

- **One source of truth:** stable ChannelId, verified graph revision, one native PCM callback, one approved `/preview/`. Apply control intents atomically with ACK/readback; reject without damaging previous accepted graph.
- **Audio RT:** no allocations, locks, disk I/O, decoder, FFT/time-stretch, vendor construction/destruction, JS or logging in ASIO callback. Precompute DSP, use bounded worker PCM rings and frame-aligned publication. Mic monitoring must not gain latency when file player is enabled.
- **Reduce patch debt:** repair root ownership/callback problem rather than layering a second compatibility route, manual setup switch or “temporary” alternate renderer. Keep focused tests of core invariants for every structural change.
- **User mental model:** drop song → Play → output. Native engine builds trusted default MUSIC channel/wire itself; preserve user-changed SOURCE ONLY/BUS/MAIN MUTE and never change hardware output covertly. Errors have one inline contextual action, not Lab/admission/checkbox.
- **Design-to-code path:** trace actual production host/source paths, not detached classes or demo HTML. Prefer minimal coherent refactor with clear ownership and provenance. New native commands belong to existing bridge/dispatcher contract.
- **Budget discipline:** tests first at cheapest relevant layer, batch related changes into one useful PR revision, choose expensive qualification only at an actually runnable milestone, inspect failures before retry, deliver physical-test package early.

## 6. Autonomous decision/stop policy

Proceed without asking for a repeated plan or user approval when the user already requested development and the next scope is clear; do not ask the user to run CI manually. **Stop only for true external blockers** such as untrusted device identity requiring an endpoint choice, unavailable connector permissions, impossible-to-merge branch protection, or physical tests that cannot be simulated. Describe the precise blocker and complete all other feasible work. CI failure is normally an agent debugging task, not a reason to stop and ask a user to act.

Release sign-off remains distinct from implementation completion: source merge, CI success, verified downloadable ZIP, and independently witnessed physical ASIO/OBS testing are different facts. Report each accurately.

## 7. One-screen handoff required after actual work

```text
Goal and user-observable change:
Repository, current main SHA:
Active product PR/branch/exact HEAD SHA:
Actual source changes and focused regression fixtures:
Fast Linux UI/native: PASS/FAIL/PENDING or not applicable + run URLs
Heavy CI chosen: UI / NATIVE / FAT / NONE; exact SHA + job/result URLs
Executable: verified unqualified Windows artifact link or not yet built
Physical ASIO FAT: witnessed PASS / PENDING / FAIL
Blocker (only if real) + next directly actionable milestone:
```

**The core KPI is first playable, reliable user workflow per useful PR; CI minutes and number of commits are costs, not progress.**
