# ArSonMiX — Next-thread / Integration Handoff: Audio Hardware I/O
**Current branch:** `feat/studio-hardware-io-matrix-20261030` from `main@0801cc8`. **Owner constraint:** Studio One-style function and visual density; maintain ArSonMiX brand/assets and original safe native architecture.

## Before touching code
1. Read `AGENTS.md`, [PRD](PRD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), [ARD](ARD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), current `web/mixer-io.js`/`css`, `web/monitor.js`, `src/Main.cpp`, `CMakeLists.txt`, `tests/mixer_io_smoke.py`; reconcile current `main`, merged #198/#201, open MS3B #203, and DYN1A #199.
2. Validate: `node --test tests/mixer_io_operator_contract_test.mjs`, `python tests/mixer_io_smoke.py`, `python scripts/build_ui_css.py --check`, existing Windows CMake/MSVC/CTest build. Obtain exact-head CI evidence; no unnecessary repeated CI. Static tests are not hardware signal measurements.
3. If tests find a failure, edit focused file(s) only; preserve accepted live graph and fx bank. Re-run failed focused tests before further CI.

## Shipped in proposed PR
- Actual M/L/R input crosspoints for verified pin columns, honest temporary incomplete-stereo mode, source mode per row, limited 8-column bank but no phantom/ordinal mapping.
- Actual MAIN L/R output crosspoints with duplicate rejection; no invented MONITOR/CUE output sockets.
- Device tab and native JUCE DRIVER SETTINGS; accessible dropdown alternative; REVERT DRAFT; real native APPLY gated by mute and ACK.
- Compact dark UI, original generic SVG illustration embedded in native assets. SC220 specific picture intentionally not passed off as physical device identification.
- Focused Node contract and Playwright integration regression.

## Next acceptance gate on actual Windows machine
- ASIO audio interface 2-in/2-out: verify observed device/backend name and all enabled real input/output pins; map CH1=Mic1 MONO, CH2=Mic1/Mic2 stereo; APPLY while MUTED, verify native ACK then manually unmute at low analog volume; negative duplicate MAIN output must be rejected.
- 8-in/8-out interface with sparse enabled pin mask: verify input columns/pagination, map non-adjacent L/R and MAIN HW5/6 to physical jacks; verify HW1/2 silent and no phantom pins, negative stale epoch on disconnect.
- Simultaneously test VST3 insert state/CH1/CH2/AUX sends, 1366×768 integrated GPU cursor responsiveness, and **zero-FX physical crackle #15** separately. Do not close #15 solely because GUI/CI succeeds.
- Product name on new copy: **ArSonMiX**. Existing brand casing in legacy docs/build names should be normalized carefully in a separate bounded branding task, not a risky global string replace.
