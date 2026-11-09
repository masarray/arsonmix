# Vocal Throw M1–M4 × current main — exact-ancestry integration

**Parent 1:** protected main `381c140200d2f4ecb965183a2da1ba1e46d5d9c2` (Compressor restart #489, Harmony #467, other merged improvements).
**Parent 2:** unmodified M4 branch head `5f6b1b01ba41a5cc559f0335d38b052967949f55` (includes native M1/M2/M3 and approved hardware faceplate M4).
**Delivery branch:** `integration/vocal-throw-m1-m4-main-20261109`. An actual two-parent integration commit retains both histories; neither parent was rebased or force-pushed.

## Merge discipline

Three-way diff from shared ancestor `8accbc4af3391efc8c3bd8f2c99feb955e8f69db` identified 31 Vocal Throw-specific changed files, 10 of which were also modified on main. Merged all ten **semantically**, using the current main tree as the starting point. Files unique to M4 are copied from verified exact M4 tree. In particular:

- `src/Main.cpp`: both Harmony and Vocal Throw install paths/command ACK, exact-instance gate, FX snapshot, music-clock service, pending-command service and all newer Compressor meters survive.
- `src/LiveAudioEngine.h`: Harmony and Vocal Throw both use one authoritative JUCE native PCM callback and their accepted FX lease bank, not parallel engines.
- `src/NativeMixerSessionFxRestore.h`: EQ, Compressor, Harmony and Vocal Throw restore their own validated native state; external VST3 state is applied only to actual VST3 inserts, all on CONTROL before graph publication.
- `scripts/compose_native_peq.py`: the only approved GUI/picker injects all **four** native built-in FX exactly once and preserves the browser-preview-only fallback when not hosted inside JUCE.
- `web/approved-native-bridge.js`: both Harmony and Vocal Throw have distinct native editor actions and catalog identities; no browser-generated audio/DSP.
- `site/preview-v2/assets/mixer-contextual-buttons.css`: preserves Harmony CSS plus the EQ/Compressor/Vocal Throw shared Power/Close family, including accepted bypass/readback colors and geometry.
- `site/preview-v2/assets/mixer-fx-rack.css` and `mixer-native-compressor.js`: retain newer compressor/harmony improvements with M4 hardware face added, no regression to older compressor JS.
- `CMakeLists.txt`, `.github/workflows/ui-smoke.yml`: union of native realtime and visual checks; don't replace latest main CI policy.
- `tests/native_peq_assembly_test.py`: extends existing mandatory quick test to enforce Harmony + Vocal Throw in a single GUI, native catalog/restore paths, and same CSS Power/Close owner.

## Release gates (not waived)

The previous green M1 63/63 and M2/M3 64/64 MSVC CTest apply to **old source SHAs**, not this merged commit. The M4 Chromium GUI PASS also predates integration. This new merge requires exact-SHA native Windows compile + CTest, UI assembly/Chromium desktop+mobile, and end-to-end native driver/source tests. On real ASIO hardware, test input→FX→BUS/MAIN, SOURCE ONLY and WIRE retarget, Vocal Throw tail, device changes, music beat detection and xrun metrics; simulated tests do not certify stage performance. Do not merge/ship without the gates. Preserve existing M1–M4 PRs Draft as recovery anchors.

**Reference:** https://github.com/masarray/arsonmix/issues/473#issuecomment-6737134260
