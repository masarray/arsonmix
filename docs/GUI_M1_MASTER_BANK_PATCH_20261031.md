# GUI-M1 — Persistent MASTER / Input Navigation / Truthful PATCH

Date: 2026-10-31. Exact base main c5d982a364accbe940aa60ce5b8b1ff10079a398. Single integrated GUI milestone.

## Operator behavior
- MASTER stays visible on the right while input channel bank scrolls. Preserve eleven L2 seven-row faders, rulers and meter geometry.
- Sidebar channel selection and returning from PATCH/FX RACK reveals selected strip without a full mixer re-render, DSP mutation, plugin remount, timer or animation.
- PATCH INPUT has one header/crosspoint column per verified physical device port (up to eight per page) and no synthetic blank columns on small hardware.

## Reference and restrictions
- Waves eMotion LV1 fader bank ergonomics; Studio One native hardware matrix clarity; W3C WCAG 2.2 2.5.8 target size guidance (not a compliance certification). No proprietary visual assets.
- Preserve L2 #176, native MUTE→APPLY→revision/epoch ACK, plugin state, output endpoint and zero-fx hardware issue #15.

## Tests
- node --test tests/mixer_shell_contract_test.mjs
- python scripts/build_ui_css.py --check
- python tests/mixer_shell_geometry_smoke.py
- python tests/ui_smoke.py and python tests/mixer_io_smoke.py
- Single Chromium CI on PR head; inspect 1680/1440/1366/1024 screenshot modes. Windows device FAT remains a separate gate.
