# P1E integration reconciliation — 2026-10-28

## Source of truth and reasons for a fresh branch
Base: `main@61b2971e2be276c7b4e7fa034b64640ae1ef848e`, including folded-metal fader #120, fast-fall/ease-out meter #106, independent MASTER L/R, latest Orbit/capture #105, de-click #107, backup #116, P1D #114, pre-fader hardening #118, lean native CI #101.

The original P1E PR #119 had Windows native CI PASS [40608546403](https://github.com/masarray/arsonmix/actions/runs/40608546403) but browser UI smoke FAIL [40608546037](https://github.com/masarray/arsonmix/actions/runs/40608546037). Its `tests/ui_smoke.py` was stale and failed at a preexisting peakHold assertion (`Boolean(peakHold.masterL) is False`); #119 has no UI smoke file diff. Its source branch was built atop earlier `main@4951f3b`, before later main fader changes.

We compared the blob SHA at that base and the newest main for **every P1E-modified existing file**: `.github/workflows/p1-windows.yml`, `CMakeLists.txt`, `src/Main.cpp`, `src/LiveAudioEngine.h`, `web/monitor.js`. All five were byte-identical on latest main and old base. Thus their exact P1E candidate blobs can be safely reused; the other six files are additive P1E-only. This integration intentionally retains **all remaining file blobs from latest main**, especially the validated `tests/ui_smoke.py`, `web/app.js`, `web/bridge.js`, `web/styles.css`, fader CSS, and Orbit assets. No merge of old PR branch history or old test baseline.

## Actual scope
The opt-in **SIGNAL CHECK → DRY ROUTING LAB** provides real physical ASIO/WASAPI enabled input-channel mapping into independent 10-strip dry DSP through the preallocated P1D `PreparedMixerBlock`. Operator mute, zero loaded VST3, DIRECT route, no independent clock, native epoch and graph revision/ACK, stale-device fail-closed behavior are required. C++ route publisher uses a single atomic writer/read reservation gate and off-RT reclamation. Native callback path remains ordinary CH1 VST3 when LAB is off, and no unsupported GUI multi-VST3 claims are added. Failure, unconnected input or stale port mapping must produce silence, not fake VU activity.

## Tests and risk gates
The exact newly integrated head must run the **same** full Chromium/Firefox/Orbit GUI smoke currently green on main, native Node route contract, Windows MSVC/CTest and real executable packaging. Verify output against latest original main tree for unrelated visual source blobs. No additional CI matrices or jobs.

Do **not** claim full production 10-channel VST3, 2-AUX runtime processing, plugin graph live swap, or #15 zero-VST crackle hardware pass. P1E is operator-controlled DRY hardware FAT only. Physical Windows ASIO/WASAPI inputs, buffer dropouts, latency, mute transition and routing status must be tested on owner's actual interface. Avoid arbitrarily modifying old PR #102 FX drag engine, source fader image or Orbit; use current main as owner.
