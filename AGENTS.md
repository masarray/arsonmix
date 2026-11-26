# ArSonMix — SINGLE APPROVED GUI: /preview/
**BINDING OWNER CORRECTION 2026-11-04 — overrides prior incorrect /preview-v2/ target.**

<!-- ARSONMIX_CROSS_THREAD_POLICY_20261126 -->
## Mandatory cross-thread integration discipline (owner directive, 2026-11-26)

**Every ArSonMix agent/thread must read the current [INT-1 coordination policy, Issue #837](https://github.com/masarray/arsonmix/issues/837) and live main's AGENTS.md at invocation/resumption and before integration/build/release.** The living issue records the canonical release candidate; at this directive it is [PR #899](https://github.com/masarray/arsonmix/pull/899), branch `staging/io-reverb-integrated-global-20261125`. Never hardcode a stale SHA; refresh live refs and CI. Once merged, use current main or the explicitly recorded successor. Historic DAG notes are not authority to restore obsolete code.

- **Baseline and scope:** inspect main, canonical RC, own PR and related peer heads/base/diff/checks before coding. Start new product work from compatible current integrated source. Reconcile divergent work by common-base hunks, not copying an old Main.cpp, CMakeLists.txt, FX registry/presets/session, audio engine or web bridge wholesale.
- **Preserve progress:** maintain accepted GUI, stable FX/ChannelId/EdgeId, installed native ACK/readback, durable .smix, single primary render authority and bounded realtime processing. Verify both features on overlapping files, retain source refs and explicitly account for deferred work. Re-read source and destination heads before integration; non-force expected-SHA update or protected expected-head merge only. If heads move, recompute; never overwrite, force-push, delete or silently close unfinished work.
- **No qualification churn:** while canonical exact-source full Windows/installer testing is active, work independently on scoped branches; no cosmetic/research promotion that cancels useful qualification. Supersede only for an identified blocker with documented reason and new exact SHA. Batch actual fixes; avoid duplicate heavy native/FAT dispatches. A cancelled/stale/isolated run or successful status publisher is not a passing exact candidate.
- **One release lane:** no independent release candidate for the same product or speculative #450 release trigger. Keep experimental DSP and dual-ASIO research separate from production admission until proven. Preserve protected merge checks, physical ASIO/listening/rollback gates and owner-authenticated release controller [#450](https://github.com/masarray/arsonmix/issues/450). Prioritize actual release blockers; publish only exact qualified assets and truthful release scope.
- **Handoff:** on meaningful milestone, report own PR exact HEAD and main/RC refs, overlapping paths and reconciliation, retained/deferred changes, actual CI evidence, installer identity if available, blockers and next action to own PR; update INT-1 when dependencies change. Do not call a prior installer latest.

Parallel development remains autonomous: no file lock, global mutex, new approval step, extra CI requirement or remote chat control. The policy requires reconciliation at integration and evidence before release, not permission to start work. Existing engineering and owner delivery constraints remain in force.
<!-- END_ARSONMIX_CROSS_THREAD_POLICY_20261126 -->

<!-- ARSONMIX_INT1_AGENT_BRIDGE_20261120 -->
**CURRENT CROSS-THREAD DELIVERY TARGET (2026-11-20):** [INT-1 — one user-testable Windows integration preview](https://github.com/masarray/arsonmix/issues/837). Agents resuming G-MV, N-MOD, multi-device I/O, Graphic EQ or relevant FX/GUI PRs should read this living GitHub coordination issue, refresh actual `main` and PR/check/review heads, fix the nearest meaningful blocker, and deliver an exact-SHA integrated Windows preview as soon as the *qualified* primary ASIO/GUI/session paths permit. This is a coordination link **not a new approval gate, branch/file lock, CI waiver or automatic message to other chats**. Do not delay a safe single-ASIO alpha for unproven multi-vendor hardware or unaccepted Guitar FX visuals; do not merge stale drafts wholesale, invent FAT results or bypass existing protected release controls.

**BINDING OWNER DELIVERY POLICY 2026-11-09:** [Autonomous Fast Delivery Contract](docs/AUTONOMOUS_FAST_DELIVERY_CONTRACT_20261109.md). A ChatGPT agent should code autonomously toward one *usable product milestone* with the relevant engine/GUI/tests on one coherent PR; do not stop for a separate architecture-spike approval, Lab/admission, owner to press Actions, or one PR per small patch. Proceed with independent coding while quick CI is pending; resolve real failures before heavy qualification/merge. Run only the relevant exact-SHA milestone UI, native or FAT; when a Windows installer is wanted, choose **FAT instead of a duplicate native** run and deliver user-test build as soon as M1 works. All physical identity, single callback, approved /preview/, branch protection, true CI and physical FAT rules remain mandatory.

**ONLY approved/public/product GUI: https://masarray.github.io/arsonmix/preview/**. There must be ONE public preview, ONE actual rendered GUI, and ONE native Windows GUI. **/preview-v2/ is rejected**: do not display, promote, publish or base native design on it as a separate UI. Do not substitute another console from `web/index.html`, `web/mixer-layout.js`, `web/console-native.html`, PR #278 or old M3/V48 mock. **The JUCE ENGINE must stitch into the EXACT approved /preview/ GUI**, never redesign it.


## REQUIRED owner-facing completion message — every task, every ChatGPT thread (2026-11-18)

**The owner is not expected to interpret Git/CMake/PCM details. On completing (or failing/partially completing) ANY substantial assigned task, the working agent MUST send a clear, Indonesian-language closing report in its own chat response.** Also place an equivalent brief completion note in the feature PR body or PR Conversation when a real GitHub milestone was written and connector access is available. **This is mandatory communication, never a file/domain lock, owner permission requirement, special CI status or cross-thread waiting step.** The agent may work on any area of its own branch immediately; reconciliation happens at the actual merge with normal functional checks.

The final response must explicitly state: **(1) Hasil yang terlihat oleh pengguna; (2) Status nyata:** DESAIN SAJA / KODE DIBUAT / KODE DIUJI / MERGED MAIN / INSTALLER TERSEDIA / HARDWARE TERUJI / DIRILIS; **(3) Aman dipakai?** YA/BELUM/TIDAK, explaining which part; **(4) Tindakan Anda:** one specific safe action or **TIDAK PERLU MELAKUKAN APA-APA**; **(5) Bukti:** exact PR/branch/commit, meaningful CI/PASS/FAIL/NOT RUN and hardware FAT when applicable; **(6) Langkah berikutnya** and retained constraints. Explicitly say **BELUM** when only source/docs exist. Differentiate merge, compiled artifact, installed app and physical two-vendor ASIO proof; do not say users can test from a nonexistent build.

See [Operator Task Handoff & Multi-Device I/O Continuity](docs/architecture/OWNER_FRIENDLY_TASK_HANDOFF_AND_MULTI_IO_20261118.md) for compact examples and source-proven IO-0 through IO-5 progress slices. For multi-device, preserve the existing single native render graph, primary direct ASIO, and separate capture-/sink-only I/O callbacks; the merged [multi-device ARD](docs/ARD_ARSONMIX_MULTI_DEVICE_IO_V2_20261118.md) is the canonical technical definition. **The design is NOT an implemented or hardware-qualified dual-ASIO feature.** Other threads may keep coding while tests/peers run; this reporting rule must NEVER be implemented as an API-dependent admission gate, ownership token, wait for an old draft, or global mutable status file. A thread cannot automatically send alerts into another independent conversation; the completion is reported in the active chat and discoverable GitHub PR.

## Binding contextual-console owner decision — 2026-11-13

**READ FIRST when working on input GAIN, FX RACK, Sends on Faders, PRE/POST, WIRE SEND, or related UI/PCM:** [Owner-approved Contextual Console PRD](docs/PRD_CONTEXTUAL_CONSOLE_GAIN_SOF_20261113.md) and [companion ARD](docs/ARD_CONTEXTUAL_CONSOLE_GAIN_SOF_20261113.md). These are **normative feature specifications** (not a claim the native DSP is already implemented); they supersede inconsistent older v5/MS5 contextual UI/tap descriptions for that scope. Preserve the exact single approved assembled `/preview/` mixer and hardware PNG/SVG styling; GAIN/FX RACK are one interlocked shared strip area, SOF is a target-specific fader-flip layout, and PRE/POST/ON/level belong to the selected **EdgeId**. PRE_FADER (post-insert) is NOT legacy PRE_FX. SOF minimum/OFF never deletes WIRE; no auto-patching. Real native controls require installed PCM ACK/readback.

**2026-11-13 implementation starting point:** at that checkpoint [#604](https://github.com/masarray/arsonmix/pull/604) was a browser reference UI draft and [#606](https://github.com/masarray/arsonmix/pull/606) was the more advanced in-scope integration draft **including #604** and SonKu. Refresh live main and PR heads first; reconcile the most advanced source without reimplementing it, losing plugin/editor work, or double-merging. **Naming across the repository and CI:** ArSonMix is the mixer. Harmonice is its built-in vocal harmony plugin. Use mixer terminology for shared controls, including MUTE/SOLO/PFL and SOF, and name the actual subsystem in test labels (plugin DSP, mixer PCM, ASIO). Do not schedule Windows builds for specification-only changes.

### Accepted GAIN GUI checkpoint / cross-thread preservation — 2026-11-14

**When working on GAIN, GUI integration, Pages, SonKu, LMS, FX or native controls, consult [cross-thread safety ledger #699](https://github.com/masarray/arsonmix/issues/699) and re-audit LIVE `main` plus relevant PR files.** The accepted operator GAIN baseline was shipped via #690 → #694 → #696 → #698 (`a01fe350` at that moment): static steel PNG knob with shadow, 58px default cap / 84×25px recessed green-on-black LCD and 30-segment LED arc, no click-to-seek, 6px intentional drag arm, 0dB capture ±1.6dB / release ±3.2dB, and native ACK/readback. Use the current approved `/preview/` and targeted visual/gesture tests; *do not replay these historical PR commits*.

**High-risk old integration branches:** draft #661 and #649 carry pre-checkpoint GAIN HTML/CSS/bridge; draft #635 changes Pages assembler/CI, while SonKu P7–P8 is a stacked, physically unqualified research/field chain. Reconcile only relevant, source-owned hunks onto the latest `main`; do not copy whole old GUI files, merge the stacked drafts en masse, force-push or overwrite other threads. This ledger is a collision warning, **not a new coordination/owner gate**. Continue independent work normally and treat any physical audio FAT separately from Chromium/UI success.

## Multi-device I/O + maintainable MVVM boundaries — 2026-11-18

**Binding architectural route for new ArSonMix mixer I/O and relevant maintainability work once this guidance is merged:** [full multi-device PRD](docs/PRD_ARSONMIX_MULTI_DEVICE_IO_20261118.md), [full multi-device ARD v2](docs/ARD_ARSONMIX_MULTI_DEVICE_IO_V2_20261118.md), [current-source research](docs/RESEARCH_ARSONMIX_MULTI_IO_MVVM_20261118.md), [incremental modular-monolith/MVVM ADR](docs/architecture/ADR_MODULAR_MVVM_FEASIBILITY_20261118.md), and existing [maintainability epic #527](https://github.com/masarray/arsonmix/issues/527). Earlier [input-only PR #757](https://github.com/masarray/arsonmix/pull/757) is narrower **draft research**, not an installed multi-ASIO implementation. Before coding, refresh latest `main`, active branch heads, current /preview/ owner, device/routing/FX code and real physical FAT evidence. Historic documents do not prove present device capabilities.

**Ownership precision — ONE DSP renderer, potentially multiple physical I/O callbacks:** The invariant is exactly **one accepted native ChannelId/EdgeId PCM/VST3 graph, one graph-processing primary render-clock authority, and one approved /preview/ GUI**. A separately opened hardware device may need a **capture-only producer callback** or an **output-only consumer callback** with a dedicated bounded FIFO/ASRC; those may be permitted **only after implementation and physical proof**, and MUST NEVER execute another mixer/DSP/VST3 render graph. Historic 'no second callback/AudioDeviceManager' instructions elsewhere prohibit second **mixing/render owners**; they do not categorically prohibit separate proven I/O-only device managers/adapters in this scoped feature. This clarification does not enable unsupported ASIO drivers or arbitrary duplicate output owners.

**Three different capabilities, never conflate:** (a) a single multichannel primary ASIO device's physical input/output PINS are same-clock/direct; (b) today's optional `LiveSecondaryOutputDevice` is a **WASAPI MAIN-copy** sink; (c) additional independently-clocked ASIO/WASAPI **capture devices** and independently routed **BUS/Channel output sinks** require a new, proven multi-device design. No ASIO 16-slot backend capacity claim proves vendor compatibility. Do not build or require a custom signed kernel/virtual audio driver, external ASIO4ALL/VB-Cable, or change JUCE frameworks solely for feature support.

**Device lifecycle & RT non-negotiables:** preserve zero-added-FIFO primary ASIO fastpath; one serialized CONTROL-owned device+binding transaction with actual driver/pin/rate/buffer readback, identity/generation/physical-output authorization, negative ACK/rollback, non-destructive .smix offline intent and isolated secondary failure. Compile immutable source/sink maps on CONTROL; capture/output callbacks may only copy bounded PCM via preallocated single-producer/single-consumer bridge, with independent drift correction per clock domain. Never allocate, wait, lock, call COM/registry/driver reopen, create/destroy VST3 or touch WebView/JS inside audio callback. No accidental speaker auto-arm, no fallback to a different named device, no repeated driver reopen to evade an incompatibility. ASIO+ASIO success, ASIO+WASAPI, output sink BUS tapping, drift/CPU/latency, hotplug and FX once-only need **independent exact-binary Windows hardware FAT**, not browser/CI simulation.

**MVVM is UI-only; modular ownership is native:** Existing JUCE + WebView2 + approved /preview/ remain. Extract small pure JS/TS ViewModels for *presentation*, transient drafts, selectors and pending/rejected/accepted visual state; C++ native session/graph/readback remains sole accepted source of truth. Application use-case boundaries and device adapters may be modularized incrementally; DSP/PCM/VST3 must NOT become ViewModels or synchronous UI observers. Do not rewrite WPF/React, rebuild GUI assets, split huge files cosmetically, duplicate state machines, transport envelopes, projection readbacks, plugin leases or already merged M1–M6 work. Each extraction must **remove old responsibility**, keep same behavior/ABI, and supply targeted regression evidence (native .smix/ACK/PCM, GUI, RT, device as applicable). Preserve one canonical native console-v8 requestId/epoch/revision contract and uint64 string identities.

**Execution / cross-thread handoff:** deliver one bounded implementation milestone in its existing owning PR, or one dedicated integration PR if no active owner; inspect exact changed-file overlap and current SHA to avoid clobbering the active Milestone D/FX/Guitar/SonKu threads. No unconditional requirement for a separate architecture-spike approval: run the minimal vendor-ASIO feasibility probe as an early falsifiable part of the scoped milestone, then proceed only with supported evidence. Do not claim dual-ASIO or independent BUS-output support merely because its PRD/ARD exists. Record relevant code path, actual source->channel->FX->WIRE/BUS->output PCM trace, exact SHA, CI lane, hardware compatibility/FAT pending or proven, rollback and next verifiable product increment. Docs-only edits follow the existing **no expensive CI** policy.

## Long-term maintainability execution — 2026-11-11

For real source ownership and future feature-extension work, read [Current Architecture](docs/architecture/README.md), [Maintainability Execution Blueprint](docs/architecture/MAINTAINABILITY_EXECUTION_V1_20261111.md), and [Fast Feature Playbook](docs/architecture/FEATURE_EXTENSION_PLAYBOOK.md). A [ready-to-copy next-thread implementation prompt](docs/architecture/NEXT_THREAD_EXECUTION_PROMPT.md) begins coding M1 instead of repeating architecture research. [Issue #527](https://github.com/masarray/arsonmix/issues/527) tracks progress. These are implementation aids, NOT an extra coordination gate or an override of the approved /preview/, single native PCM callback, hard RT/audio safety, owner-authorized smart CI, or physical FAT rules. Keep the plan updated after proven source changes; do not create a new high-level plan instead of shipping a testable milestone.

## New ChatGPT thread / repository-agent entrypoint — MUST READ FIRST (2026-11-06)

This is the **binding workflow for a new ChatGPT thread working through the connected
GitHub app**, not an optional prompt to be re-invented per session. The agent
must **act on the repo**, not ask the owner to operate Actions manually:

1. Read this `AGENTS.md`, [Smart CI runbook](docs/SMART_CI_RUNNER_WORKFLOW.md),
   and the actual default-branch `.github/workflows/chat-ci-milestone.yml`,
   `p1-windows.yml`, `ui-smoke.yml`. Read the relevant code and tests as well.
2. Fetch **fresh** `main` SHA, open PRs, latest active integration PR HEAD,
   changed files, CI jobs, artifacts and recent work before any edit. Historical
   handoff SHAs/PR states and old workstream plans are **not live authority**.
   Reuse a currently active in-scope integration PR if present; **old P0 #418
   is historical and not a mandatory present-day entrypoint**. For a distinct
   feature (e.g. native ArSon Player), one scoped integrated product PR is
   allowed; no stacked mini PRs. Never overwrite another agent's HEAD; use
   an expected-SHA lease and reconcile concurrent changes.
3. Execute one meaningful vertical product milestone, with targeted engine,
   bridge, GUI and tests together; use M0 technical tracing **inside the M1
   implementation PR**, not as an extra signoff. Continue independent coding
   while fast CI runs. Use the GitHub connector to commit/reconcile/inspect
   and merge a scoped, **actually verified** PR when permitted. Deliver the
   first working installer early via one deliberate FAT lane, not repeated
   Windows jobs. Never equate green CI with physical audio FAT.
4. **CI cost contract:** relevant PR pushes get lightweight Linux checks;
   no per-commit Chromium, Windows MSVC or ZIP. Docs/instructions-only edits
   normally need diff and contract inspection, **not** a Windows/Chromium
   run. Run expensive tests only at substantial milestones after quick checks
   pass, selected by actual modified surface; avoid redundant/repeating runs.
5. The connected GitHub app has **no direct `workflow_dispatch` operation**:
   the assistant triggers milestones itself by an exact-SHA, owner-authorized
   PR Conversation comment (below). Do not toggle Ready-for-review, push
   empty commits, or ask the user to click Actions merely to start CI.
   After posting, inspect the dispatcher, chosen workflow and artifact. Report
   queued/running/failed as such, and **do not claim PASS prematurely**.
6. Work autonomously *within the active chat task*; do not promise unseen
   background work. If connector permissions or owner-authenticated dispatch
   are unavailable, state the blocker rather than inventing a successful run.

**Mandatory handoff on every substantive completion:** repo/PR/branch,
current `main` and PR HEAD SHAs, changed files, targeted test evidence,
expensive CI lane actually dispatched (or explicitly skipped), FAT status,
outstanding blockers and next meaningful milestone. Keep canonical /preview/
and one native PCM/VST3 routing authority unchanged unless the milestone
requires a tested modification. Legacy [multi-agent coordination
policy](docs/workstreams/MULTI_AGENT_COORDINATION.md) is **SUPERSEDED**;
it cannot add reviewer/runner/owner barriers.

## One integration owner — binding product recovery (2026-11-05)

Previous four-lane ENGINE / INTEGRATOR / FAT / GUI ownership and active-PR
collision gates are **superseded**. A single product integration PR may edit
engine, native host, GUI data bridge, tests, packaging and CI as necessary.
The `<!-- arsonmix-integration: single-owner -->` marker identifies the
current integrated P0 PR for historical traceability; it is **not a CI
admission or owner-approval gate**. The old cross-thread coordinator was
retired. Fast contracts run on relevant PR commits; expensive Windows,
browser and FAT qualification use the selective exact-SHA milestone below.
No waiting for abandoned thread approvals; no fake milestone/progress claims.

Before a change: fetch the latest main HEAD and current active PR status,
compare relevant earlier integrated work **as currently present in main**,
reconcile source file-by-file, and preserve functional VST3, Show, device
selection and the accepted GUI. Use **one integrated PR** with meaningful
product changes. Branch protection, review, exact-SHA tests, source integrity
and independently witnessed hardware FAT remain binding. No forced merge,
no uncontrolled callbacks, no silent auto-arm or output reroute. If real
hardware is unavailable, mark physical audio FAT **PENDING**.

## Smart CI milestones — GitHub connector triggers *selectively*, not repeatedly

**SMART CI 2026-11-22:** The owner-only PR comment dispatcher also accepts
`/arsonmix-ci check|fast|full <exact HEAD SHA>`. `check` inspects mandatory
existing quick checks without building; `fast` runs the already-approved
FIELD portable **without full Windows CTest** only when native product files
changed; `full` selects the required MSVC/CTest and/or Chromium lane by
actual changed files, after all relevant quick checks pass. `fat` remains
the explicit full installer/ZIP gate. Keep unchanged exact-SHA source,
deduplication, approved sound, protected merge and hardware FAT semantics.
See `docs/SMART_CI_RUNNER_WORKFLOW.md` for exact commands and limits.


**23 Nov 2026 GENERAL WINDOWS FAT CORRECTION:** The generic `.github/workflows/p1-windows.yml` no longer has a plugin-specific Harmonice FAT switch, compile step or CTest lane. Use `/arsonmix-ci feature <exact HEAD SHA>` for an isolated, reviewed single-feature source diff: auto-select the real feature's declared CMake/CTest targets, without building the entire mixer or installer. Unknown, mixed, core/shared engine and CMake changes cannot silently use selective qualification: use `native`/`full`. Deliberate `fat` still runs the **complete** native+installer qualification (including every active feature), with unchanged source/ZIP/upgrade verification. A scoped feature run is visibly labeled **NOT full**, cannot satisfy the protected MSVC full gate, and proves neither installer nor ASIO hardware. Do not dispatch a costly Windows job merely because CI source files changed.

**23 Nov 2026 CI OWNER CORRECTION — IMMERSIB:** do not use `fat` to validate a JUCE ImmerSIB editor/reopen/PCM fix. It incorrectly consumes the full global `p1-windows.yml` compile, unrelated Vocal Harmony job labels, all native CTests and installer path. Use owner comment `/arsonmix-ci immersib <exact current HEAD SHA>` for a strictly scoped ImmerSIB PR: 4 real JUCE/PCM CTests only, no `ARMixConsole` build, Vocal Harmony, or installer. `fat` is denied by the dispatcher for isolated ImmerSIB source scope. The owner can use `/arsonmix-ci stop <exact current HEAD SHA>` to cancel only matching active CI. **Focused is not full native merge gate**; preserve protected CI/real ASIO qualifications for intentional integration releases. No automatic paid Windows job on PR pushes.

**Per-commit development = fast, path-scoped Linux contracts only.**
No automatic heavy Chromium, Windows MSVC/CTest, or Windows artifact ZIP on
ordinary PR synchronize, even if a PR is Ready. `main` release qualification
is independent and stays enforced on relevant main changes.

**Milestone lane-selection policy (one expensive lane per actual need):**

- **Docs/instructions-only:** inspect precise diff and references; no CI
  dispatch. Never create a dummy commit to obtain a check.
- **GUI/CSS/WIRE visual or gesture behavior:** `ui` after fast UI passes;
  browser screenshots and real pointer/interaction assertions are required.
- **Native C++/audio/ASIO/device/VST3/IPC/control behavior:** `native`
  after fast native passes; build MSVC and run realtime CTest.
- **Need an executable ZIP for a real hardware test:** `fat` after fast
  native passes; this already includes MSVC, CTest, FactoryFX integrity and
  independently verified ZIP. **Do not also dispatch `native` for the same
  SHA merely to repeat the build.** For a mixed GUI+native milestone needing
  both, dispatch `ui` plus **either `native` or `fat`**, once each, only
  after checking whether suitable same-SHA runs already exist.

When a meaningful milestone is ready, use the **connected GitHub connector**:
read the exact current 40-char PR HEAD and corresponding quick check(s);
inspect recent `workflow_dispatch` runs and earlier milestone PR comments;
then post **one top-level PR Conversation comment**, exactly one line, with
the selected command:

```text
/arsonmix-ci ui <exact current PR HEAD SHA>
/arsonmix-ci native <exact current PR HEAD SHA>
/arsonmix-ci fat <exact current PR HEAD SHA>
```

On `main`, the owner-only `ChatGPT CI Milestone Dispatcher` validates the
repository, source PR, exact SHA, failed-quick gate and duplicate commands,
then issues the workflow dispatch. **The ChatGPT agent does the triggering;
the human owner does not have to press Run workflow.** An old, non-owner,
forked or malformed request must fail closed. Do not issue duplicate
lane+SHA comments or blind reruns. If a command exists already, track that
run rather than re-dispatching. Fix a genuine failed fast contract before
requesting a costly lane. If HEAD changes after qualification, qualify the
new exact HEAD at its next meaningful milestone.

Finally inspect the dispatcher run, downstream Windows/Chromium job log and
artifact via GitHub. Always distinguish `scheduled`, `in_progress`, `PASS`,
`FAIL` and `skipped`. An **UNQUALIFIED** ZIP is not physical ASIO listening,
latency, routing or crackle proof. Never silently promote/release a product
with pending physical FAT. Full procedure and edge cases:
[Smart CI runner workflow](docs/SMART_CI_RUNNER_WORKFLOW.md).

## Verified current published GUI implementation
`site/preview/index.html` was merely a wrapper/iframe pointing at a generated `/preview-buttons/index.html`. `scripts/build_pages.py` generates that **approved PNG hardware-button variant** using current `site/preview-v2/index.html` as an INTERNAL HTML/WIRE/FX/fader source, with specifically `site/preview-buttons/assets/mixer-control-keys.css`, `mixer-control-keys.js` and `mute-solo-pfl-56x37.png` as approved button source. **The assembled result behind /preview/ is the visual authority, not /preview-v2/ in isolation.** There must be exactly ONE deterministic assembled output and no independent published variant. Internal source files may survive only as build inputs until safely consolidated; they are NOT alternate product GUIs. **Current approved visual including PNG keys must never be silently replaced by the base variant.**

## Active contract and ownership
- [P0 native GUI stitch #389](https://github.com/masarray/arsonmix/issues/389) — compile the **exact /preview/ assembled GUI** and component assets into JUCE WebView2, no new layout/renderer. Update the issue from earlier incorrect preview-v2 wording.
- GUI reference maintenance: currently reviewed `main` component/source changes. Publish ONLY `/preview/` and no independent `/preview-v2/`, `/preview-buttons/` or historical Vxx GUI. Old URLs may redirect to /preview/ but never contain another mixer UI.
- ENGINE [#266](https://github.com/masarray/arsonmix/pull/266) + [#387](https://github.com/masarray/arsonmix/pull/387) keep ONE primary JUCE realtime callback, ONE accepted ChannelId/EdgeId PCM graph, ONE genuine native VST3 lease/editor; do not ship ENGINE's own `web/console-native.*` visual renderer.
- FAT [#270](https://github.com/masarray/arsonmix/pull/270), native epic #192, hardware crackle #15. One integrated exact-SHA Windows EXE; exact approved generated `/preview/` HTML/assets SHA256 and screenshot/interaction parity; native PCM + VST3 + actual hardware FAT.

## Native architecture and user behavior
- Bundle exactly the SAME generated GUI shown at `/preview/` into native WebView2 using a deterministic current-main source/asset provenance; do not hand-copy old mock HTML or duplicate CSS ownership. Thin native adapter may change **data and event authority only**, not GUI appearance or physical interaction.
- v8 schemaVersion=6 `arMixConsoleSubscribe`, `Snapshot`, `Intent`, `Ack`; canonical uint64 decimal-string ChannelId/EdgeId/device epochs/graph revisions. Intent accepted only after matching ACK and later accepted same-revision native snapshot. PENDING/REJECTED/STALE/unsupported != LIVE.
- Default CH→MAIN is real graph edge. Display WIRE only for the selected channel; decorative source/master MAIN cable fragments are hidden by owner correction (2026-11-07), while actual MAIN edges and jack controls remain; collapsed wire quiet; OUT lit only for **custom non-MAIN** outgoing edges. Wire, OUT, IN, Matrix, Navigator, Scene/Undo, FX, fader, PAN, meter derive from native accepted graph/control state. Physical device pin mapping only actual verified ASIO/WASAPI ports; no fake meter/audio/FX in Pages.
- Structural graph off callback, atomic immutable publication; zero callback locks/heap/UI/VST scanning/disk. Preserve CH1 vendor VST3/Orbit, original 10CH+4AUX, Show/device paths during migration. Never create duplicate routing PCM or special fake CH1; no JS/WebAudio audio engine.
- One meaningful vertical milestone: actual compiled Windows EXE displays exact /preview/ skin (including its approved buttons), then real native input→channel→MAIN, meter/fader, VST3 and WIRE intent/ACK/snapshot, then full FAT. Verify 1366×768, 1680×900, 1920×1080 and bank/density/fold states.
- `node scripts/check_console_puzzle.mjs --release` must fail on any alternate GUI. No user UAT until a coherent native product; physical rig absent => FAT PENDING, not product ready.

**Current execution:** [single handoff](docs/workstreams/HANDOFF_APPROVED_PREVIEW_V2_NATIVE_20261104.md) is retained as filename for link compatibility but CONTENT MUST point to /preview/ alone. Older v5/v6/v7 and historical PR #278/#271 GUI paths never override this document; audio/safety requirements still apply. **No one may revive /preview-v2/ as a second GUI**.

## Historical diagnostics retained (not GUI authority)
The archival `docs/GUI_BASELINE_LOCK_2026-10-25.md` remains available only for regression forensics and former native asset compatibility. It NEVER outranks the user's approved **/preview/** assembled GUI or its PNG keys.

### Historical physical audio diagnostics (not GUI product authority)
- [2026-10-25 Smart Audio Engine ARD](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md)
- [Smart Audio Engine Implementation Blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md)
- [Dry physical crackle #15](https://github.com/masarray/arsonmix/issues/15)
These remain for device-clock, render-discontinuity and hardware FAT engineering only. Their old GUI v0.8 descriptions do not override the sole **/preview/** GUI.

## Owner correction: native FX editing (2026-11-07)

Continue the working #418 ChannelId PCM integration. Vendor editor lifetime follows the actual active processor lease across reorder/transfer; never validate it against inactive M4 slots. FX remove/reorder/cross-channel MOVE must preserve exact instance identity, state and bypass and publish through native ACK/readback. A row-sized pointer ghost is presentation only. The owner explicitly authorized reconciliation and merge of this verified milestone to main; stable-release/physical-FAT claims remain separate. See [P3 evidence](docs/P3_NATIVE_FX_RACK_EDIT_20261107.md).
