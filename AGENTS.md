# ArSonMix — SINGLE APPROVED GUI: /preview/
**BINDING OWNER CORRECTION 2026-11-04 — overrides prior incorrect /preview-v2/ target.**

**ONLY approved/public/product GUI: https://masarray.github.io/arsonmix/preview/**. There must be ONE public preview, ONE actual rendered GUI, and ONE native Windows GUI. **/preview-v2/ is rejected**: do not display, promote, publish or base native design on it as a separate UI. Do not substitute another console from `web/index.html`, `web/mixer-layout.js`, `web/console-native.html`, PR #278 or old M3/V48 mock. **The JUCE ENGINE must stitch into the EXACT approved /preview/ GUI**, never redesign it.

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
   Continue the existing applicable integration PR (currently P0 #418; verify
   its state each session), rather than stacking duplicate PRs or redoing
   completed work. Do not overwrite another agent's newer HEAD; use an
   expected-SHA lease and reconcile concurrent changes.
3. Plan one meaningful vertical product milestone; make targeted changes and
   tests together. Use the GitHub connector to create/commit/update a branch,
   inspect failures and PR evidence, and merge a **scoped, actually verified**
   PR when appropriate. Never equate green CI with real physical audio FAT.
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
