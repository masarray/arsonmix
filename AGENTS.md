# Repository instructions — ArSonMix (all future threads and AI coding agents)

**MANDATORY: Read [docs/GUI_BASELINE_LOCK_2026-10-25.md](docs/GUI_BASELINE_LOCK_2026-10-25.md) before editing UI or reconciling VST3 engine PRs.**

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

## If you are a future ChatGPT thread
1. Fetch live repository `main`, this file and the lock document. Cite approved commit and deployment metadata accurately.
2. Check current native PRs / base graph before cherry-picking; do not accidentally erase more recent GUI or engine work.
3. Propose and implement only the requested incremental changes, validate actual behavior, and capture new checkpoints if another GUI revision is approved.
4. Distinguish *GUI-preview simulation* from *live native audio* in reports and releases.
