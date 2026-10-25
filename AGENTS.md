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

## P0 live dual-clock + recovery checkpoint (2026-10-26)

- **Current live engine truth:** [PR #35](https://github.com/masarray/arsonmix/pull/35) merged two actual independent Windows Audio shared capture/render JUCE callbacks with the prepared `AdaptiveClockBridge` in a render-master path; the prior text describing it as standalone applies only to older commits. This is an opt-in **BETA**, not automatic device qualification. Direct ASIO path has no SRC.
- **Required current handoff:** [P0 DIRECT restore recovery & next gates](docs/P0_DIRECT_RECOVERY_HANDOFF_2026-10-26.md). On an independent device loss, fail silent and restore the previous DIRECT setup only on the message thread using bounded generation-safe backoff; never hot-plug spin, auto-renable independent mode, or mutate devices in callbacks. Expose recovery history through native COPY FULL REPORT.
- The next P0 gates are stable endpoint identity, real AUTO/STABLE capability integration, backend render underrun provenance, long-drift/quality measurements, and one consolidated physical FAT. P0 #15 remains open and no blanket crackle-free claim is permitted. Other threads may concurrently change CSS: fetch latest main and never overwrite their GUI decisions.

## If you are a future ChatGPT thread
1. Fetch live repository `main`, this file and the lock document. Cite approved commit and deployment metadata accurately.
2. Check current native PRs / base graph before cherry-picking; do not accidentally erase more recent GUI or engine work.
3. Propose and implement only the requested incremental changes, validate actual behavior, and capture new checkpoints if another GUI revision is approved.
4. Distinguish *GUI-preview simulation* from *live native audio* in reports and releases.

## Post-lock native integration update (PR #13, 2026-10-25)
The **experimental** P2.5 JUCE VST3 engine has now been integrated into `main` using the approved v0.8 GUI as its ancestor; it does not supersede the visual approval or certify audio. Archived stacked PRs and the v0.5 integration PR #11 remain as lineage, not a source for overwriting the GUI. Work on later engine phases starts from latest `main`, tests with both Windows MSVC and the GUI lock Chromium workflow, and preserves all locked visual geometry. Do not merge a stale P2.5 branch wholesale back into main.

## 2026-10-26 CSS ownership: mandatory source workflow
- Edit `web/styles-src/` modules, not generated `web/styles.css` or standalone HTML. Run `python scripts/build_ui_css.py`; `--check` is required in CI and publishing.
- `20-button-180.css` is the **sole owner** of shared button skin and ON/OFF state. OFF is square solid black crystal with subtle upper inset reflections (color `#171c23`), ON remains approved vivid inset, radius 3px, no dome. Preserve PAN 13/15/12, 168px master and fixed button geometry.
- Historical rules quarantined in `10-console-legacy.css` remain candidates, not automatically dead code. Migrate only after DOM/cascade reachability and computed geometry assertions. See `docs/CSS_ARCHITECTURE_MILESTONES_2026-10-26.md`.

- 2026-10-26 latest visual contract: Button 180 OFF, ON, pressed, and selected use **one identical inset box-shadow** and no transforms or geometry differences. OFF neutral graphite `#292d34`, ON only changes semantic color. Hover in both states changes brightness only, not border. This explicitly supersedes historical raised-OFF instructions.

## P0 independent callback-silence watchdog (2026-10-26)
- P0 independent JUCE clock adapter detects missing capture or render callback progress by sampling existing atomics at 12Hz on the message thread. Three seconds of absent progress latches a fault and enters existing bounded DIRECT restore; not a driver-confirmed xrun.
- No new realtime clock reads, heap, blocking, workers, CSS/GUI edits. Callback-silence source and duration appear in COPY FULL REPORT recovery event. Physical FAT remains outstanding.

## P0-02 ASRC signal integrity continuation (2026-10-26)
- Non-finite capture PCM is sanitized at the independent FIFO ingress and counted, without heap/locks, before poisoning polyphase FIR/VST3; invalid latency and excessive capacity are rejected before allocation.
- `bridgeNonFiniteCaptureSamples` is source-labeled, not a driver xrun. A deterministic spectral/injection test exercises bandpass, alias rejection, and finite output. See `docs/P0_ASRC_SIGNAL_INTEGRITY_2026-10-26.md`.
