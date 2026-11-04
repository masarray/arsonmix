# CANONICAL HANDOFF — /preview-v2/ → real JUCE mixer
**2026-11-04 · P0 #389 · binding GUI decision · NOT an audio release claim**

**ONE product UI:** https://masarray.github.io/arsonmix/preview-v2/ . Actual source is latest qualified `main/site/preview-v2/index.html` and `site/preview-v2/assets/**`. It is not a design proposal. **Native engine must adjust underneath; do not make or ship another GUI.** `/preview/` PNG-button variation, PR #278/#285 studies, native old `web/index.html` / `web/mixer-layout.js`, ENGINE `web/console-native.*`, and stale M3 ports are retired as product GUI sources. Preserve working native logic only.

## M1 — Deterministic GUI source in executable
1. Read fresh main and current GUI/asset hashes, not frozen PR #278 or M3. Generate a native bundle from current source with exact HTML and all CSS/JS/SVG/PNG dependencies; allow a separately loaded **thin bridge** but **no visual fork**. Proposed output: `web/approved-preview-v2/**` and SHA256 manifest.
2. Make JUCE CMake BinaryData and `src/Main.cpp` ResourceProvider serve exactly this bundle, with no old or alternative HTML entrypoint. Preserve all visual geometry, selected-only WIRE, quiet fold, non-MAIN OUT light, faders/meters, FX rack, bank, PAN, buttons, pinned MAIN, responsive density and keyboard actions.
3. Screenshot-compare actual compiled Windows EXE with browser preview-v2 at 1366×768, 1680×900, 1920×1080; expanded/collapsed, selected/unselected and bank states. Source fingerprints without actual compiled screenshot are **insufficient**.

## M2 — Accepted native adapter, not a second renderer or DSP
1. Replace *demo state authority* with native `arMixConsoleSubscribe`/`Snapshot`/`Intent`/`Ack` while keeping the reference GUI renderer and gestures. Use the existing v8 schemaVersion=6 and canonical decimal-string uint64 ChannelId/EdgeId; never map demo track indexes to native identities.
2. One native accepted `graph.edges[]` projects WIRE, OUT, MAIN, Matrix, Navigator, scenes and Undo. Default real CH→MAIN; MAIN paired fragments only for selected expanded source, anchored to inner jack dot. No folded cable noise; OUT lights only custom non-MAIN. Offscreen connections remain active.
3. Intent `requestId/deviceEpoch/baseGraphRevision/baseRosterRevision/operation/targetChannelId/payload` → matching ACK → later same-revision accepted native snapshot before committing UI. Stale/reject/device swap restores previous accepted state. No browser 'LIVE' PCM, fake ports, FX or meters.
4. Use actual verified ASIO/WASAPI active pins, installed `supportedOps`, real per-channel VST3 insertId/editor/bypass/ordering and native meters/PCM. Disabled features remain disabled until real native support.

## M3 — Keep sole engine, preserve audio
- ENGINE PR #266 and native WIRE #387: one installed JUCE callback, ChannelId/EdgeId immutable PCM graph, no added samples per internal routing hop without plugin latency; off-thread prepare/atomic publish, no RT locks/allocations, no feedback or duplicate dry. Keep current CH1 vendor VST3/Orbit, fixed 10CH+4AUX, Show and device features until migrated without regression.
- Native backend and GUI consumer must agree on `graph.channels[]`, `edges[]`, mainChannelId, `enabled`/`signalKind`, accepted revisions and truthful readiness. Explicitly reconcile C1 raw nativeBus format vs GUI validSnapshot; do NOT set `productionCallbackLive=true` to bypass capability checks.

## M4 — One true Windows release gate
- FAT PR #270 must verify one exact joined SHA, one genuine C++ producer fixture→GUI adapter→ACK/readback→FAT validator, UI screenshots/gestures, source/asset manifest, native PE resource embedding and identical Windows binary SHA.
- Prove real PCM direct vs 1/2/3-hop sample alignment, fan-in/out, cyclic/duplicate/stale rollback, two genuine VST3, measured meters, real physical selected output, Show/Undo and 0FX crackle issue #15 with actual ASIO/WASAPI hardware/soak. Do not infer acoustic clean from CPU or browser success.
- `node scripts/check_console_puzzle.mjs --release` MUST reject any alternate native GUI. If physical hardware evidence unavailable, FAT = PENDING not READY. No owner test of partial mock.

## Only these workstreams stay open
- `main/site/preview-v2/**` + GUI issue #351: user-approved GUI reconciliation only.
- [P0 #389](https://github.com/masarray/arsonmix/issues/389): native GUI source bundle/adapter, **one** implementation lane.
- [ENGINE #266](https://github.com/masarray/arsonmix/pull/266) + native WIRE #387: audio backend, no competing UI renderer.
- [FAT #270](https://github.com/masarray/arsonmix/pull/270) + physical #15: one exact Windows product evidence.

Stale GUI PR #271, #356, #310/#315/#321, #278/#285 and old Vxx GUI drafts must be **closed as superseded without merging**, preserving only valuable code via selective review. **No new GUI research, redesign or duplicate product branch.**

**Report milestone, not prose progress:** current source SHA, compiled GUI SHA, screenshots from EXE, one native ACK/readback flow, real audio PCM/VST3 evidence, failing tests and exact owner. Until then: NOT PRODUCT READY.
