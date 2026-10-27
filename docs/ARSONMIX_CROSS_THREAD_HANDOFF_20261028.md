# ArSonMix cross-thread engineering reconciliation — 2026-10-28

## Latest authoritative baseline
- Base main when the fix was authored: `de683b001a5a1e5e050bc20ddb52d32b22fa58d1` (merged PR #105). Read the latest GitHub `main` again before every integration.
- Preserved: accepted dearVR PRO 2 Orbit/POLAR UX (PR #105), contextual Near/Far 2 m display (PR #103), native MASTER LED visibility (PR #104), latest approved meter/GUI. **Do not** replace source with older handoff branches.
- Hardware reality: only native Channel 1 VST3/DSP is qualified, other strips remain UI previews. No deceptive CH2-10 feature claims.
- Issue #15: zero-plugin crackles still NOT proven resolved. Hardware report: 48k/480 WASAPI DIRECT, VB Cable capture to Space Travel 2 render, 117 capture discontinuities, 15 before MARK CRACKLE; DSP callback worst 92us, no budget overruns. Backend count is CAPTURE, not measured render underruns. Existing Smart Clock Independent is a manual A/B test, not an automatic cure.

## Present changes — bypass de-click
- Root cause: `setBypass` published a bare atomic flag consumed by the RT callback, instantly changing from processed to dry samples at a buffer edge.
- New per-physical-slot `FxBypassEnvelope`: zero-latency plugin wet/dry gain crossfades over ~8 ms, equal-sum linear; high-latency plugin switches only at silence (8 ms down then 8 ms up) to avoid mixing non-aligned dry/wet and combing.
- All state owned by RT callback; control publishes only desired bypass. No locks, heap, audio-thread WebView calls, graph rebuild, restart, or additional device buffer. Dry stereo scratch preallocated on the control thread. Steady active/bypassed paths have no blend copy, bypassed plugin processBlock is skipped. Latency counter updated only on control thread.
- Graph handoff initialises bypass state for preset recall; device restarts reset envelope from actual flags. Under plugin order reconfiguration, keep transition state indexed by physical processor, not logical GUI index.
- CI: a single existing Windows pipeline builds `ARMixFxBypassEnvelopeTests` and runs CTest; existing Node contract covers callback wiring. Never create a second Windows distributor. PR screenshots/browser flow remain unaffected.

## Parallel PR integration order and conflict guard
- PR #102 (DAW drag/drop, lock-free atomic MOVE, flattened fader): OPEN. It touches `src/LiveAudioEngine.h` and `web/bridge.js`; currently GitHub says MERGE CONFLICT. Do **not** merge #102 blindly; rebase it on the accepted main and preserve the bypass-envelope code and physical-slot association; run both reorder and bypass tests after reconciliation. Its structural ADD/REMOVE implementation remains distinct from ON/OFF bypass; do not claim audio click-free on structural operations by fixing bypass only.
- PR #106 (meter ballistics): OPEN, Chromium PASS; do not overwrite current GUI CSS/JS and do not merge until Windows/native tests succeed and current main is reconciled.
- PR #101 (lean CI): OPEN, needs independent review and status/required-check compatibility. Do not cherry-pick old source or trigger multiple Windows builds.
- Other legacy PRs (#97/#60/#59/#29/#26/#25 and old P1/P2 PRs): historical/experimental; check staleness and conflicts before any rebase/merge.
- This handoff is a snapshot, not a guaranteed global lock. Any thread must read current main SHA and open PR status at time of change.

## FAT acceptance
1. Add a low-latency VST3, start steady music/voice, toggle ON/OFF multiple times, listen for click at the transition. Try rapid toggle and multiple FX.
2. Repeat with high-latency plugin; a brief ~16ms level dip is expected; hard crackles are not.
3. Verify no CPU spikes, overruns, latency regressions or Orbit interruption. Test actual ASIO/WASAPI devices; browser smoke cannot establish acoustic absence of clicks.
4. Separately test zero-plugin WASAPI crackles per Issue #15, with DIRECT vs opt-in Independent mode and output-only tone diagnostic.
