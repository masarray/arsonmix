# Repository instructions — ArSonMix (all future threads and AI coding agents)

**MANDATORY: Read [docs/GUI_BASELINE_LOCK_2026-10-25.md](docs/GUI_BASELINE_LOCK_2026-10-25.md) before editing UI or reconciling VST3 engine PRs.**

**MANDATORY FOR NATIVE AUDIO WORK (2026-10-25):** Read the canonical [Smart Audio Engine ARD v1.0](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md) **and** [Smart Audio Engine implementation blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md), then review [P0 issue #15](https://github.com/masarray/arsonmix/issues/15). Native 0-FX Windows Audio crackling is hardware-confirmed; `main` is experimental, **not** live-certified. This new ARD supersedes conflicting claims in historical v0.3. Do not work on P2.6 12-channel DSP until P0 dry I/O acceptance.

## Approved visual contract
- GUI v0.8 approved by the product owner on 2026-10-25. Original exact source commit: `343692fc9b702661a8455cca30f0ffd108907324`; merged to `main` via PR #2 (merge `aff95a7c811a3817edf61303e1f24ef31e53d11d`).
- Permanent recovery reference: `checkpoint/gui-v0.8-approved-20261025` (never update or delete it). New contributions start from latest `main`.
- Maintain a 12-input + 1 master aligned strip console; 6 visible FX slots on each; master FX, PAN and stereo L/R; one top toolbar; compact left rail; fixed MUTE label; live insert power state; no-jump fader drag; contextual, realistically shaded controls and clear selected border.
- The streaming analyzer has **only LUFS-I and dBTP**. In the browser label values as simulated; do not invent actual DSP meter readings.
- Never copy stale UI files from the native branch over the approved GUI. Native integration must adapt the engine events and visuals to this baseline **without regressing GUI or changing its design without user request**.
- Source modules: `web/index.html`, `web/styles.css`, `web/app.js`. Self-contained offline distributable: `AR-MIX-Live-FX-Host-Standalone-v0.3.html` (historical name). Keep CSS/JS/HTML byte-for-byte synchronized with the standalone. `site/` supplies landing and review shell; `scripts/build_pages.py` copies the standalone.
- No universal button CSS override; preserve the original file assets and physical sizes. Native GUI changes must preserve layout and usability at 1680x920 and 1440x820.
- Run `node --check web/app.js`, all PNG/standalone asset checks, `python tests/gui_baseline_lock_test.py`, and `python tests/ui_smoke.py` before merging a GUI PR.
- GitHub Pages production deploys **from `main` after a green main Chromium smoke**. Do not create a competing preview-branch publisher.
- Native JUCE/VST3 PRs #1 and #3–#11 were **not** validated or merged by GUI PR #2. Do not mark them done or close them until native hardware, RT safety, actual meter/bypass and plugin tests pass. Reconciliation PR #11 is built from an older GUI and must be rebased/reworked against this lock.

## Strict audio-engine engineering rules (do not lose this across threads)

- P0 first: measure and isolate output-only vs capture-only vs dry-through with **zero VST3**, including backend-provenance telemetry. `callbackGapCount=0` is not proof of a clean output; do not call scheduler gaps true xruns.
- Default AUTO/STABLE profile: capability-aware and explained; direct zero-extra-FIFO path for single actual clock; cross-clock FIFO+drift estimator+ASRC only when proven necessary; cold-start/driver change rollback with generation tokens.
- Every audio path preallocated, bounded and allocation/lock/I/O-free in the host callback; third-party VST3 must be measured and vendor APIs respect proper thread affinity. No routine `removeAudioCallback/addAudioCallback` on FX insert/removal/reorder.
- One testable scope per PR. Deterministic fake clocks, bounded buffer/latency, device-state tests, Windows MSVC+CTest+Chromium PASS, actual sound FAT **separate**. Do not claim "optimized" merely because CI is green.
- Locked GUI v0.8 source untouched without explicit new visual approval. Engine status goes through native adapter. No stale branch bulk merges.
- Use `docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md` PR ladder and definition of done, and update it only with evidence/ADR when changing design decisions.
- Consolidate P0 infrastructure development and automated CI before requesting another physical hardware test; no user-assisted test after every tiny code edit. Native SIGNAL CHECK should expose **COPY FULL REPORT** to capture hardware capabilities, source-labeled xruns, budget histograms, and latest MARK CRACKLE events in the OS clipboard.
- Read [P0 Smart Audio diagnostics & prepared bridge](docs/P0_SMART_AUDIO_DIAGNOSTIC_AND_BRIDGE.md) before wiring an SRC. The `AdaptiveClockBridge` is **experimental standalone**, and MUST NOT be wired to the joined JUCE callback or claimed to fix physical crackling until separate-clock adapter feasibility, back-end xrun semantics, automated quality benchmarks and hardware FAT are demonstrated.

## If you are a future ChatGPT thread
1. Fetch live repository `main`, this file and the lock document. Cite approved commit and deployment metadata accurately.
2. Check current native PRs / base graph before cherry-picking; do not accidentally erase more recent GUI or engine work.
3. Propose and implement only the requested incremental changes, validate actual behavior, and capture new checkpoints if another GUI revision is approved.
4. Distinguish *GUI-preview simulation* from *live native audio* in reports and releases.

## Post-lock native integration update (PR #13, 2026-10-25)
The **experimental** P2.5 JUCE VST3 engine has now been integrated into `main` using the approved v0.8 GUI as its ancestor; it does not supersede the visual approval or certify audio. Archived stacked PRs and the v0.5 integration PR #11 remain as lineage, not a source for overwriting the GUI. Work on later engine phases starts from latest `main`, tests with both Windows MSVC and the GUI lock Chromium workflow, and preserves all locked visual geometry. Do not merge a stale P2.5 branch wholesale back into main.
