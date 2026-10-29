# ArSonMix — Windows Live Mixer & Native VST3 Host

**October 30, 2026 engineering status:** ArSonMix is an experimental Windows-first **10 input channel + 4 stereo AUX return + MASTER** mixer with real device-bound audio routing and VST3, not a hardware-qualified live/broadcast release. The native hardware 0-VST crackle issue [#15](https://github.com/masarray/arsonmix/issues/15) remains open. **Never interpret successful Windows CTest, GUI screenshots, or a workflow named Hardware FAT as proof of clean real-device audio.**

The [approved GUI visual baseline](docs/GUI_BASELINE_LOCK_2026-10-25.md) remains the reference for compact FX racks, BTN-180 and layout; its historical channel count is superseded by the current mixer PRD.

## Start here — new canonical product and engineering plans
1. [PRD v2.0 — Digital Mixer, per-channel FX/ROUTE/SENDS, custom hardware I/O, AUX buses](docs/PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md)
2. [ARD v2.0 — one audio graph, physical output patch, bus model, native ACK, staged execution](docs/ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md)
3. [Next-thread execution contract](docs/EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md)
4. [FX Rack / Routing cross-thread regression guard](docs/FX_RACK_ROUTING_CROSS_THREAD_CONTRACT_20261030.md)
5. [Smart Audio Engine safety architecture](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md) and [real hardware FAT runbook](docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md)

The user-provided eMotion LV1, Studio One, Audition/DAW and FL-style wire references define the desired **workflow**, not an instruction to clone their artwork. Primary UX: stable MIX surface; FX/ROUTE/SENDS mode toggle, separate FX Rack and Routing workspaces, real INPUT/OUTPUT visibility in main window, native mono/stereo device matrix and proper AUX return processing. **Do not make Routing replace a channel's working VST3 inserts.** Do not expose raw JUCE audio configuration as the end-user product experience: keep JUCE audio backend but build an original capability-aware ArSonMix I/O screen.

## Correct current feature boundary
- **Actual engine foundation:** ten native input lanes, four dedicated stereo AUX FX returns, bounded PRE/POST CH→CH and CH→AUX sends (32 edges), verified enabled ASIO/WASAPI DIRECT source pins, epoch/revisioned route ACK and safe muted/unpatched startup. These AUX returns are **internal mix paths**, not independently addressable physical headphone/output jacks.
- **VST3:** CH1 has an existing working **multi-insert native vendor VST3 chain** with editor, reorder, bypass and Orbit for eligible DearVR plugins; the routed callback still processes it. Other routed input/AUX lanes currently support one native effect per lane/return. Their additional FX rows must not be advertised as eight active inserts before actual multichannel expansion.
- **GUI:** MIX, FX RACK, ROUTING, SETUP and Show exist as evolving surfaces, with approved tactile metal faders, native meters and shared BTN-180 commands. As of this documentation update the requested comprehensive per-channel FX/ROUTE/SENDS switch and dedicated hardware I/O UI are **design goals, not completed features**.
- **Routing output:** current MAIN L/R readback is **read-only**. Extra physical outputs, monitor/CUE, GROUP/DCA/MATRIX, arbitrarily routed AUX output and plugin delay compensation require further implementation and native tests.
- **Show / portability:** experimental portable VST Studio and DRY native Show Save/Browse/Recall exist. Full VST3 binary-state session restore with output pin remapping and transaction rollback remains incomplete.
- **Real-hardware quality:** low-end GPU UI and 0-FX ASIO/WASAPI crackle need separate physical FAT. Unmeasured render XRUN must not be reported as confirmed zero.

The original 2026-10-25 [v0.3 ARD](docs/ARD_AR_MIX_LIVE_FX_HOST_v0.3.md), historical GUI v0.8 lock and P1A/P1H stacked PRs remain engineering evidence for their dates. **Their CH1-only and old 12-channel/13-strip statements are superseded for current progress** by this status and actual latest main source. Never bulk merge historical unreviewed PRs into current main.

## Browser GUI and Windows host
- GUI preview: https://masarray.github.io/arsonmix/preview/ (visual preview may simulate meters and routing; not proof of real DSP).
- Standalone GUI: https://masarray.github.io/arsonmix/preview/mixer.html
- Actual source and releases: https://github.com/masarray/arsonmix
- Windows native executable packages: https://github.com/masarray/arsonmix/actions/workflows/p1-windows.yml — choose a **successful, exact-main-SHA artifact**, verify BUILD_PROVENANCE.json. It is still experimental and requires real hardware testing.

## Engineering workflow / CI economy
Read [AGENTS.md](AGENTS.md). Check latest main SHA, PRs and Actions before edits. Respect `web/styles-src` CSS ownership and generated `web/styles.css`; use shared `.btn-180` material. Do not replace the mixer HTML, introduce UI overlays hiding VST3 chains, or add multiple DSP callbacks. Target complete vertical slices, quick targeted local tests, then one exact-head Chromium and Windows MSVC/CTest/package build if needed; avoid needless CI and micro-PR churn.

```bash
node --check web/app.js
node --check web/bridge.js
node --check web/monitor.js
python tests/ui_smoke.py
```

The full native CI verifies MSVC/CMake/CTest, GUI bundle and package provenance; CI does not certify real ASIO/WASAPI continuity. Hardware test: SIGNAL CHECK → MARK CRACKLE → COPY FULL REPORT, share device type, rate, buffer, audio path and an honest pass/fail.

## License and bundled assets
Keep original source/asset licenses and SDK notices. A public ArSonMix installer ships **without** the owner's personal VST3 files and presets; individual users may create their own portable private VST collection. Do not redistribute commercial VSTs, LV1 visuals or third-party proprietary screenshots without rights.
