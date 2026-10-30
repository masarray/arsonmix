# ArSonMix L2 — LV1-inspired mixer shell and vertically linked faders (2026-10-30)

## Engineering decision

The live mixer has exactly eleven visible strips: ten sources plus MASTER. A strip contains seven vertical bands, and **every strip uses one shared CSS grid track definition**:

1. Channel header/identity number.
2. Upper multipurpose bank: FX rack, routing, or AUX sends. MASTER occupies the same track even if its content remains the master FX rack.
3. Pan/input/control section.
4. MUTE/SOLO or MASTER MUTE/STEREO.
5. dB gain readout.
6. Dynamic fader, LED meters and calibrated rulers.
7. Bottom channel identity.

The fixed tracks are uniform across all eleven strips. Switching bank changes only a shared `--ar-upper-height`, leaving the fader as the common `minmax(110px,1fr)` area. The fader PNG travel, meter mapping, knob controls and C++ audio callback remain untouched. This fixes the historical independent MASTER flex shrink.

## Original UI hierarchy informed by Waves eMotion LV1

Reference: Waves eMotion LV1 Mixer Window overview and Layer Mode documentation. The guiding interaction is an upper bank that changes by mode while the fader strip remains spatially continuous. This is **not** reuse of Waves source code, assets, visual copyright, or a promise of feature parity.

- Top: MIXER, FX RACK, PATCH as three existing navigation views, segmented together.
- Left rail: FX / ROUTE / SENDS per-channel processing modes. The controller object from G1/G2 is moved *whole* to preserve existing delegated listeners and `monitor.js` native transaction handling.
- Left rail utility footer: MY SOUNDS, PLUGINS, SETUP, SIGNAL CHECK. Existing snapshot and preset commands remain in the left rail.
- All action controls retain ArSonMix BTN-180. Status and hardware controls use existing bindings.
- No second signal graph, no independent routing state, no fake physical outputs, no front-end polling.

## Provenance / concurrency — reconciled after merge

**MERGED + APPROVED as current GUI baseline:** [PR #171](https://github.com/masarray/arsonmix/pull/171) squash `2ddd434ebb7bfa1b98d9396ff15c79ba857ca12b`, verified by Chromium workflow [40976822422](https://github.com/masarray/arsonmix/actions/runs/40976822422) **SUCCESS** and Windows MSVC/native-RT/CTest/package workflow [40976822381](https://github.com/masarray/arsonmix/actions/runs/40976822381) **SUCCESS**. Permanently tracked in [Issue #176](https://github.com/masarray/arsonmix/issues/176).

The predecessor [#167](https://github.com/masarray/arsonmix/pull/167) (G1/G2) and [#168](https://github.com/masarray/arsonmix/pull/168) (G3 native AUX SENDS inspector) were both merged BEFORE L2. L2 deliberately did **not** overwrite their `web/mixer-io.js` or `web/mixer-io.css` and instead puts `web/mixer-layout.js` / `web/mixer-layout.css` after those modules. The main-branch [ADR R5 #172](https://github.com/masarray/arsonmix/pull/172) and product orchestration #177 are compatible architecture/docs work; future R5 source/channel/bus-format UX MUST retain this exact shared fader/meter grid and no phantom physical port mapping.

**Future-thread contract:** read this document and #176 first, preserve all eleven lower-strip coordinates and muted native ACK; no extra mixer toolbar, shorter MASTER, duplicate UI controllers, more costly blur, or pasted proprietary eMotion LV1 assets. This document asserts only GUI/CI evidence; physical 0FX hardware crackle [#15](https://github.com/masarray/arsonmix/issues/15) remains unqualified.

## Acceptance gate

`tests/mixer_shell_geometry_smoke.py` checks bounding-box equality for header, upper bank, controls, MUTE, numeric readout, fader, scale, meter and channel name on CH1–CH10 and MASTER at four viewport sizes and all three banks, including a full `renderMixer()` redraw. It checks that SENDS shrinks **all** faders and returning to FX expands **all**. Screenshots plus existing `tests/ui_smoke.py`, `tests/mixer_io_smoke.py` and native MSVC/CTest must pass. Physical ASIO continuity remains a separate FAT; success here does not clear issue #15.

## Verified visual and regression gate

- `tests/mixer_shell_geometry_smoke.py` measured all eleven sources and MASTER at 1680×920, 1440×768, 1366×768 and 1024×720 across FX / ROUTE / SENDS and repeated `renderMixer()`, with aligned fader, meter, scale, mute and bottom identity (<1.1px tolerance).
- `tests/ui_smoke.py`, `tests/mixer_io_smoke.py`, `tests/mixer_shell_contract_test.mjs`, resource/standalone parity, native WebView2 package checks all passed at the L2 HEAD prior to merging.
- Protect lower-strip alignment when ADR R5 adds mono/stereo input source and channel-processing-format selectors: use the focused channel panel or shared upper bank, **not independent track heights**.
- No real physical hardware quality claim. The native MSVC build and package are engineering checks, not user ASIO FAT.
