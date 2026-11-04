# START HERE — THE ONE APPROVED GUI IS /preview/ (2026-11-04)

**Final owner decision:** https://masarray.github.io/arsonmix/preview/ is the ONLY approved preview and Windows GUI/UX. **/preview-v2/ was explicitly rejected** and may not be shown as another public GUI or native design target. The actual approved /preview/ screen includes **PNG hardware MUTE/SOLO/PFL controls** that are not the bare preview-v2 rendering.

**How to source the approved visual:** the ONE checked-in source is `site/preview/index.html` with `site/preview/assets/**` (approved PNG keys included). `scripts/build_pages.py` publishes it; `scripts/build_approved_native_gui.py` consumes the same Pages output for JUCE. Do not resurrect retired preview-v2/buttons sources.

## Four meaningful milestones (do not re-plan)
1. **One public preview:** `python scripts/build_pages.py` generates /preview/index.html directly. `python tests/single_preview_publication_contract_test.py dist-pages` must verify PNG buttons, WIRE and all legacy redirects. Never serve a separate /preview-v2/ mixer.
2. **Real Windows GUI:** Compile exact approved /preview/ HTML+assets into JUCE BinaryData with source SHA256 manifest, native ResourceProvider and real EXE screenshot/interaction parity at 1366x768, 1680x900, 1920x1080 and bank/density/fold. No old `web/index.html`, `web/mixer-layout.js` M4 or `web/console-native.html` as alternate renderer.
3. **Native accepted adapter and audio:** v8 schemaVersion=6 intent→matching ACK→later same revision accepted Snapshot, canonical uint64-string ChannelId/EdgeId; correct stale/reject rollback, real channel/WIRE/MAIN/FX/faders/meters/I/O, one native JUCE callback/VST3 lease. Default MAIN real edge, selected-only wires, quiet collapse, OUT lit only custom non-MAIN edge. Engine #266, WIRE #387, no JS DSP. Preserve working CH1/10CH+4AUX, Show and devices during migration.
4. **Joined FAT:** GUI #389 + ENGINE #266/#387 + FAT #270, one exact-SHA Windows executable; genuine native C++ producer→GUI→FAT fixture, real PCM impulse/fan-in/out/2 VST3, ASIO/WASAPI, #15 crackle and soak. `node scripts/check_console_puzzle.mjs --release` rejects another GUI. No user UAT until integrated proof; physical FAT unavailable = pending.

**Never create or merge another GUI.** Close stale GUI branches; retain useful native code only. Green browser simulation or isolated native CI does NOT make a product ready.
