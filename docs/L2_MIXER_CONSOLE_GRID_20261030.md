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

## Provenance / concurrency

This is a presentation-only L2 change on the main branch following merged #167. #168 implements G3 AUX sends on `web/mixer-io.js` and `web/mixer-io.css`; L2 deliberately does **not** edit those files. New `web/mixer-layout.js` and `web/mixer-layout.css` load after the G1/G2 modules, so a later G3 merge retains its inline editor unchanged.

## Acceptance gate

`tests/mixer_shell_geometry_smoke.py` checks bounding-box equality for header, upper bank, controls, MUTE, numeric readout, fader, scale, meter and channel name on CH1–CH10 and MASTER at four viewport sizes and all three banks, including a full `renderMixer()` redraw. It checks that SENDS shrinks **all** faders and returning to FX expands **all**. Screenshots plus existing `tests/ui_smoke.py`, `tests/mixer_io_smoke.py` and native MSVC/CTest must pass. Physical ASIO continuity remains a separate FAT; success here does not clear issue #15.
