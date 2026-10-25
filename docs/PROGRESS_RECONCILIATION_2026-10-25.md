# Main v0.8 + P2.5 native reconciliation — merge candidate

- **Non-negotiable GUI source:** `main` at `aff95a7c811a3817edf61303e1f24ef31e53d11d` (merged GUI v0.8 PR #2).
- **Native engine parent:** `feature/p25-live-preset-graph-swap` checkpoint `7a769105595aeaa2f8d8e3a549143426fdba5eae`; the MSVC CI green integration checkpoint is `351eb79bd8df775aadb4810c1cf53f7410cea411`.
- Merge strategy: retain `main` v0.8 `web/app.js` semantics, `web/styles.css`, `web/index.html` layout, `site/` and visual tests; add native C++ engine, backend bridge, tester, docs, and only the fake-meter guard / bridge script references. Native output is intentionally limited to Channel 1 and Master L/R for truthful behavior.
- This is **a development merge**. No physical hardware soak; do not call the release stable.
- Both `main` and upstream GUI + engine checkpoint remain independently available and recoverable.

---

# ArSonMix — Cross-thread reconciliation ledger (2026-10-25)

This is the authoritative **development baseline** for the integration candidate, not evidence of hardware qualification.

## Canonical integration
- Branch: `integration/main-v08-gui-p25-native`.
- **Engine ancestor**: `feature/p25-live-preset-graph-swap` @ `7a769105595aeaa2f8d8e3a549143426fdba5eae`, Windows CI GREEN run [40164992415](https://github.com/masarray/arsonmix/actions/runs/40164992415).
- **GUI visual source**: `fix/fader-thumb-symmetry-polish` @ `7d4892036caadd97c79e03321e8f11de5d3d3dcf`. Restored latest channel identity editor, dedicated master strip inside the mixer, tactile hardware PNGs, clip/peak and refined PAN/faders, premium dark metal controls.
- **UI-to-engine adapter**: `web/bridge.js` inherited from P2.5 and adapted to current GUI DOM. ONLY real Channel 1/real Master measurements control the native meters. No sinusoidal demo meters run in JUCE.
- **No destructive cherry-pick**: neither GUI branch nor historic PRs were overwritten. All original branches/PRs remain readable. The integration source incorporates visual code on top of native engine with contract tests.

## Proven / supported vs incomplete
| Capability | Current integrated claim | Evidence/limit |
|---|---|---|
| GUI premium | Latest GUI branch imported into Windows host | Browser visual QA required on integration PR |
| Channel identity (name/icon/color) | Interactive UI, saved as GUI/session state | Not authoritative for audio routing |
| VU native | Ch1 input and independent Master L/R sourced from DSP | Channel 2–12 fake activity disabled |
| VST3 FabFilter/iZotope RX insert | Confirmed by user on previous P2.4.1 Windows EXE | Retest integrated EXE |
| VST3 catalog, add/open/editor/bypass | C++ native Channel 1 | Others GUI placeholders |
| `.armix` real plugin state | Native ZIP binary-state, validation and restore | Runtime vendor round-trip needs hardware test |
| P2.5 recall during output active | Prepared graph, callback-boundary fade and deferred retirement | No dual-graph crossfade; audio dips around transition; not qualified for performance |
| ADD/REMOVE/REORDER FX | Real Channel 1; callback may detach and briefly stop | Not yet live-safe |
| MASTER FX and master PAN | Attractive GUI **preview only**; controls disabled in native shell | Master fader/MUTE and true L/R meter remain native |
| Full mixer/streaming/record routing | Not implemented | Next phase P2.6 |
| 30–60 minute soak/xrun/third-party crash sandbox | Not completed | Release blocker |

## Branch and PR ancestry (DO NOT DELETE)
The following PR stack retains all historical engineering changes, and **must not be blindly merged in arbitrary order**:
1. [PR #1](https://github.com/masarray/arsonmix/pull/1) P1 native first sound
2. [PR #3](https://github.com/masarray/arsonmix/pull/3) P2 VST3 catalog and native FX
3. [PR #4](https://github.com/masarray/arsonmix/pull/4) DAW selection/scan progress
4. [PR #5](https://github.com/masarray/arsonmix/pull/5) output/diagnostics
5. [PR #6](https://github.com/masarray/arsonmix/pull/6) realtime telemetry/auto route
6. [PR #7](https://github.com/masarray/arsonmix/pull/7) true VU, PAN, bypass
7. [PR #8](https://github.com/masarray/arsonmix/pull/8) My Sounds native binary state
8. [PR #9](https://github.com/masarray/arsonmix/pull/9) FabFilter/RX VST3 bus compatibility
9. [PR #10](https://github.com/masarray/arsonmix/pull/10) live graph handoff.
GUI [PR #2](https://github.com/masarray/arsonmix/pull/2) is an **independent visual branch**, incorporated selectively here.
Original developer source, assets, standalone preview, docs and QA tests remain in their originating branches.

## Safety and release policy
- Keep `main` unchanged until **both** Windows native CI and Chromium visual smoke are GREEN, plus real hardware trial has confirmed VST3 insert/edit/bypass and preset recall.
- Experimental release is not "stable", no IEC/audio standards certification, no guaranteed glitch-free audio.
- One canonical artifact per promoted revision; no duplicate unverified EXE claiming latest stable.
- Next milestone after reconciled acceptance: P2.5.2 live-safe ADD/REMOVE/REORDER, worker preparation and safe device lifecycle; then P2.6 real multichannel routing, then P2.7 full mixer sessions.
