# Milestone B — Issue Triage and Release Governance

**Date:** 2026-11-17 (Asia/Jakarta)  
**Repository:** `masarray/arsonmix`  
**Immutable pre-triage main:** `d6fd94815bd0b717e3eb7acb7c97bc18e7d87db2`  
**Current public Windows release:** [`v0.5.0-alpha.26`](https://github.com/masarray/arsonmix/releases/tag/v0.5.0-alpha.26) — immutable released source `8ec5b8a3e07837c85fb6e06c952cff1621fecc51`.  
**Machine-readable evidence:** [MILESTONE_B_ISSUE_TRIAGE_20261117.json](./MILESTONE_B_ISSUE_TRIAGE_20261117.json) (all original issue numbers, per-issue acceptance, categories, dispositions).

## Executed scope and safety

**Original 27 issues** were audited, one additional precise release-governance defect [#748](https://github.com/masarray/arsonmix/issues/748) created, giving **28 inventoried issues**. Two completed historical checkpoint issues (#720 and #699) were closed as **completed**, with full original issue bodies and immutable refs retained. **26 issues remain open** (including #748). Every inventoried issue now has `area:`, `type:`, and `status:` labels; actionable open issues additionally have `priority:P0/P1/P2`. Every issue body contains a concise 2026-11-17 triage header, exact resolution criteria, and original historical detail intact. Existing `enhancement`/`backlog` labels were not removed.

This is **issue metadata, documentation and future issue intake only**. No DSP, JUCE graph, Guitar FX, approved 8D-SonKu P9.9/Venue, ImmerSIB, presets, installed Windows binary, `.smix`, installer, published Pages download or release workflow is changed. The 9 deliberately retained development PRs from Milestone A remain open. No historical spatial research is re-merged. **Do not rerun owner listening tests as though it had never happened.**

### Triage priority and evidence semantics

| Category | Count | Treatment |
|---|---:|---|
| **P0** — release/safety/core-function reliability | 8 | Highest investigation priority. If current public build is affected, block the next qualifying release until verified fixed or fail-closed; *dated historical incidents need new build-specific reproduction first*. |
| **P1** — planned feature, workflow/recovery, hardware qualification | 13 | Scoped milestone with actual ACK/PCM or FAT evidence; do not claim absent features present merely because UI demos exist. |
| **P2** — optional improvement or separate acoustics research | 5 | Explicitly not an alpha.26 production regression or permission to replace approved SonKu/ImmerSIB sound. |
| **Completed historical checkpoint** | 2 | Close issue as completed for tracker hygiene; retain exact checkpoint branch/commit and discussion. |

`status:needs-repro` and `status:field-fat` are deliberately different: one requires a fresh reproducible incident with exact version/device; the other requires **real physical-device verification**. Green synthetic CI never substitutes for physical hardware field acceptance. `status:operational` is the deliberately open release control endpoint #450, not an ordinary backlog item. `status:needs-permission` reflects a concrete repository-ruleset change not exposed by this connector.

## Exact issue inventory

| Issue | Priority | Area | Status | Release/qualification boundary |
|---|---|---|---|---|
| [#726](https://github.com/masarray/arsonmix/issues/726) | P2 | `spatial` | `research` | Not an alpha.26 gate; do not touch listening-approved SonKu P9.9 or Venue golden |
| [#720](https://github.com/masarray/arsonmix/issues/720) | Reference | `spatial` | `archived` | Completed historical reference; keep exact SHA and checkpoint branch immutable |
| [#699](https://github.com/masarray/arsonmix/issues/699) | Reference | `architecture` | `archived` | Completed cross-thread safety ledger; no live code change requested |
| [#686](https://github.com/masarray/arsonmix/issues/686) | P1 | `player` | `planned` | Player future work, not retroactively claimed in alpha.26 |
| [#683](https://github.com/masarray/arsonmix/issues/683) | P2 | `spatial` | `research` | Legacy P7.1 XYZ exploration; not a dependency for approved P9.9/ImmerSIB release |
| [#680](https://github.com/masarray/arsonmix/issues/680) | P1 | `player` | `field-fat` | Physical music/FX ASIO test not replaceable by synthetic CI |
| [#664](https://github.com/masarray/arsonmix/issues/664) | P2 | `spatial` | `research` | Historical room/dearVR comparisons, not authority to retune accepted sound |
| [#636](https://github.com/masarray/arsonmix/issues/636) | P0 | `lms` | `planned` | Safety gate before enabling any physical multichannel loudspeaker output |
| [#625](https://github.com/masarray/arsonmix/issues/625) | P1 | `updater` | `field-fat` | Published updater already exists; verify current Windows release rather than rewrite it |
| [#608](https://github.com/masarray/arsonmix/issues/608) | P1 | `guitar-fx` | `planned` | Visual acceptance pending; experimental visuals must not replace approved GUI |
| [#547](https://github.com/masarray/arsonmix/issues/547) | P0 | `vst3-host` | `needs-repro` | Historical startup and WaveShell incident; determine reproduction on exact current public build |
| [#533](https://github.com/masarray/arsonmix/issues/533) | P1 | `harmonice` | `planned` | Portions may already ship; verify current before claiming work is missing |
| [#527](https://github.com/masarray/arsonmix/issues/527) | P1 | `architecture` | `planned` | Long-term single-owner architecture; do not fork realtime callback |
| [#490](https://github.com/masarray/arsonmix/issues/490) | P0 | `mixer` | `needs-repro` | Core first-launch offline logical edit UX; current-build proof needed |
| [#473](https://github.com/masarray/arsonmix/issues/473) | P2 | `vocal-throw` | `backlog` | Optional BPM Music BUS analysis; no fake beat phase |
| [#453](https://github.com/masarray/arsonmix/issues/453) | P1 | `release` | `field-fat` | Static GitHub/Cloudflare catalogs already list 3 OS: Windows alpha.26, Linux/macOS alpha.5 |
| [#450](https://github.com/masarray/arsonmix/issues/450) | P1 | `release` | `operational` | Intentionally OPEN owner-authenticated release command endpoint |
| [#412](https://github.com/masarray/arsonmix/issues/412) | P0 | `mixer` | `field-fat` | Authoritative physical routing/SOURCE ONLY acceptance; no synthetic demo substitutions |
| [#349](https://github.com/masarray/arsonmix/issues/349) | P1 | `gui` | `backlog` | Aesthetic wire physics not a PCM routing bug |
| [#347](https://github.com/masarray/arsonmix/issues/347) | P1 | `gui` | `backlog` | Independent mixer input accessibility UX |
| [#192](https://github.com/masarray/arsonmix/issues/192) | P0 | `mixer` | `field-fat` | Dynamic track reliability with stable ChannelId; do not return to fixed ten-strip code |
| [#185](https://github.com/masarray/arsonmix/issues/185) | P2 | `factory-fx` | `backlog` | Optional native VST3 bonus, not mandatory built-in audio product path |
| [#180](https://github.com/masarray/arsonmix/issues/180) | P1 | `session` | `backlog` | Show/Scene operational safety and state recovery |
| [#135](https://github.com/masarray/arsonmix/issues/135) | P1 | `metering` | `backlog` | Display only genuine measured loudness/true peak |
| [#85](https://github.com/masarray/arsonmix/issues/85) | P1 | `vst3-host` | `backlog` | User-owned VST3 state vault; privacy and safe restore |
| [#56](https://github.com/masarray/arsonmix/issues/56) | P0 | `gui` | `needs-repro` | Old GPU stutter evidence; severity conditional on current alpha.26 reproduction |
| [#15](https://github.com/masarray/arsonmix/issues/15) | P0 | `audio-engine` | `needs-repro` | Old WASAPI/BT/virtual cable crackling; qualify exact current release before claiming present regression |
| [#748](https://github.com/masarray/arsonmix/issues/748) | P0 | `release` | `needs-permission` | Ruleset active, but zero enforced required checks and zero approvals |

Each issue has its **two scoped, testable exit criteria** directly at the top of the issue and in the adjacent JSON ledger. The title of a historical issue may be retained even if its old priority claim is no longer applicable; the 2026-11-17 label and triage header take precedence. #453 was renamed to reflect that both hosted catalogs now list Windows alpha.26 and older Linux/macOS alpha.5; remaining scope is **physical native cross-platform FAT**, not rebuilding a catalog already fixed. Research issue names were re-scoped to avoid treating unmerged P7/P8/P9 prototypes as current product status.

## Verified release-governance finding (not fixed in repository settings)

Read-only GET of active GitHub repository ruleset [#26568033, “ArSonMix - Protected Main”](https://github.com/masarray/arsonmix/rules/26568033) confirmed:

- It targets `refs/heads/main`, is **active**, forbids branch deletion and non-fast-forward update, and requires pull requests.
- Its `required_status_checks.parameters.required_status_checks` is **`[]` (zero checks)** despite `strict_required_status_checks_policy=true`. An empty required-check list does not enforce test success.
- Its `required_approving_review_count` is **0**; optional review is not equivalent to a mandatory approval.
- No bypass actors were reported by that ruleset read.

**Critical outstanding:** [#748](https://github.com/masarray/arsonmix/issues/748) tracks restoring a meaningful, tested GitHub **required merge-check policy**. The connected GitHub interface can read rulesets but has no approved ruleset write action: **this milestone does not falsely claim to have enforced new status checks or required reviewers.** A privileged maintainer must validate and apply that rule, preferably after designing a cheap unconditional status gate, with an explicit test that a red PR cannot merge.

### No-naive-required-checks principle

Current 20 Actions workflows are often **path-scoped**: [UI smoke (Chromium)](https://github.com/masarray/arsonmix/blob/main/.github/workflows/ui-smoke.yml), [Windows Native VST3 Host](https://github.com/masarray/arsonmix/blob/main/.github/workflows/p1-windows.yml), and [Smart Release/Download Contracts](https://github.com/masarray/arsonmix/blob/main/.github/workflows/release-contracts.yml) intentionally do not run on every documentation-only PR. Do **not** mark their path-filtered contexts universally required: a doc-only PR can be stuck awaiting a check that will never start. Prefer one verified **always-running fast aggregate merge gate**, which requires relevant path-scoped checks and fails closed for pending/failing/cancelled results while keeping expensive Windows compilation conditional. Such a gate requires separate engineering/CI verification and permission to change the active ruleset.

### Public release go/no-go evidence policy

1. **Source/ownership:** Release only immutable explicitly approved source SHA/tag via owner-authorized [issue #450](https://github.com/masarray/arsonmix/issues/450); never auto-release from a research PR or CI-only document merge.
2. **Code checks:** Run relevant deterministic fast GUI, logical mixer, FX/PCM, installer/license and cross-platform contracts on the **exact candidate SHA**, not stale unrelated CI; no fake ACK.
3. **Native package qualification:** Windows JUCE compiled executable, embedded AGPL/notice byte verification, exact source/manifest/checksums, Setup install/upgrade/uninstall and portable entrypoint must pass publisher-specific gates.
4. **Hardware and operator evidence:** Physical ASIO/WASAPI/device restart, gain/routing/bypass/monitoring, audible glitches and VST3 lifecycle remain independently tracked as needed; don't turn historic source-level tests into made-up physical FAT. SonKu P9.9/ImmerSIB operator-approved audio remains an accepted checkpoint, not an excuse to retune.
5. **Distribution parity:** Public release assets and both Pages static release catalogs must agree on actual newest per-platform version/URL/size/hash; preview is visual only. Windows alpha.26 and Linux/macOS alpha.5 are intentionally different versions until separately qualified.
6. **Rollback:** Preserve last qualified binary SHA, session compatibility, checkpoint refs, and release assets. A branch/PR archival change never modifies those artifacts.

### Operational command endpoint warning

Issue [#450](https://github.com/masarray/arsonmix/issues/450) **MUST remain open**: `.github/workflows/release-control.yml` only authorizes release commands attached to this exact issue number. Do not post arbitrary release-prefixed comments, close/recreate the issue, or repurpose it as the general governance board. This triage modified its **metadata and body only**, preserving all previous content, and did not submit release commands.

## Intake discipline and closure criteria

New defects should include current release/source SHA, OS/audio backend, reproducible steps, actual vs expected behavior, de-identified relevant logs and a physical- vs simulated-test distinction. New features should include explicit single-engine/GUI ownership, acceptance criteria, safety/session impact, and which worker vs RT thread owns work. **Close only after** exact-head evidence and operator sign-off when applicable; link merged PR and release/field proof. Close historical research as superseded only if its unique source/provenance remains recoverable, as demonstrated by Milestone A.

### Administrative note

The GitHub connector available for this milestone exposes issue labels/updates but **does not expose GitHub Milestones creation/update**. Therefore this is an authoritative, versioned **Milestone B governance workpack with actual GitHub issue metadata and labels**, not a false claim that all issues were assigned to native GitHub Milestone objects or that the ruleset was edited.

**Unresolved:** #748 ruleset enforcement requires authorized settings modification; physical field acceptance and open P0/P1 work remain open. This tracker milestone is successful when classification, evidence, and documentation are reconciled—not when every product defect/roadmap issue is prematurely closed.
