# ArSonMix — Windows Live Mixer & Native VST3 Host

> **CURRENT CANONICAL PRODUCT / IMPLEMENTATION STATUS — 2026-10-31:** [Cross-thread audit, live-vs-offline truth table, source ownership and operator-first M0/M1/M2 priorities](docs/PRODUCT_TRUTH_CROSS_THREAD_20261031.md). Reference implementation: `main@4acc88e2674561a9a3059ebff0c80c58c96c5269`; latest confirmed Windows native/CTest/ZIP pipeline: `498ded667`, run [41152162311](https://github.com/masarray/arsonmix/actions/runs/41152162311) SUCCESS. Current shipped live path is **10 input + 4 internal AUX**, with physical MAIN/MONITOR and one optional secondary stereo MAIN output; DYN1B ChannelId graph remains OFFLINE/PCM parity, PATCH2 direct selected-channel quick-source is **PRD/ARD ONLY**, MS5 sends-on-faders currently stages native route changes rather than realtime continuous send gains; six FactoryFX VST3 modules **not bundled** (`plugins: []`). Issue [#15](https://github.com/masarray/arsonmix/issues/15) physical zero-FX crackle remains OPEN. Build PASS is **not** real-device FAT; older milestone paragraphs below are historical snapshots and must NOT override this status.


**CURRENT OPERATOR TEST MILESTONE (30 Oct 2026, `main@20cd7878`):** MS3A native sparse-hardware MAIN output patch [#198](https://github.com/masarray/arsonmix/pull/198), DYN1A runtime-sized **offline** PCM kernel [#199](https://github.com/masarray/arsonmix/pull/199), G5E verified native routed-VST3 Show Save/Restore [#197](https://github.com/masarray/arsonmix/pull/197), and MS3A2 output-aligned FX de-click/LUFS/dBTP [#201](https://github.com/masarray/arsonmix/pull/201) have merged. Follow the **[current operator Windows FAT / acceptance guide](docs/INTEGRATED_MS3A_G5E_OPERATOR_FAT_20261030.md)**, not historical read-only output or DRY-only Show limitations lower in this README. Native fixed **10 INPUT + 4 AUX** remains the LIVE graph; stable-ID DYN1A is NOT live CH11+, CH1 borrowed legacy VST rack remains excluded from G5E routed Show. Hardware quality #15 remains NOT CERTIFIED. Only download a Windows artifact with verified matching sourceCommit and completed Windows MSVC/CTest/package; do not infer real-hardware success from CI.


**Factory FX / SIX BUILT-IN EFFECTS (owner requirement, 2026-10-30):** [Factory FX PRD](docs/PRD_FACTORY_FX_20261030.md), [ADR/packaging/manifest architecture](docs/ADR_FACTORY_FX_BUNDLE_20261030.md), [manifest schema](docs/factoryfx-release-manifest.schema.json) and [product epic #185](https://github.com/masarray/arsonmix/issues/185). Future official installer/portable must contain six FIRST-PARTY VST3: ArSonEQ, ArSonComp, ArSonVerb, ArSonHarmony, ArSonColor and ArSonKuPik Enhancer; USER-selected VST3 remain separately imported to private Studio vault #85. At audit ASKPVST public Windows v0.5.26 binary exists on vst-enhancer release; EQ is on arson-eq feature branch (main only LICENSE), Verb requires outbound licence, Comp/Harmony/Color not yet built. **No factory binaries shipped in current ArSonMix release.** Use separately verified VST3 artifacts and existing native host/G5A, not a second FX engine or public CI clone of private askp source. Preserve actual #184 MS1, #178 G5A, L2 #176, #15 real hardware safety; factory licence/activation and JUCE gates before release.


**Merged functional milestone MS1 + G5A (2026-10-30):** [MS1 #184](https://github.com/masarray/arsonmix/pull/184) delivered independently switchable native MONO/STEREO **processing channel** separate from physical INPUT source, live MIX/PATCH controls and one muted native `schemaVersion:2` route transaction, plus DRY Show snapshot V4 source/format-safe capture/recall and V2/V3 migration. [G5A #178](https://github.com/masarray/arsonmix/pull/178) delivered an internal eight-slot prepared FX foundation, **not yet** the user-editable G5B multi-insert contract or PDC. MS1 exact-head [Chromium PASS](https://github.com/masarray/arsonmix/actions/runs/40984090092) and [Windows native+CTest+package PASS](https://github.com/masarray/arsonmix/actions/runs/40984090111). Both are merged by `main@961c286d`; post-merge main integration CI and real hardware #15 require separate verification. Remaining [MS2 mono/stereo buses #174](https://github.com/masarray/arsonmix/issues/174), [MS3 output patch #175](https://github.com/masarray/arsonmix/issues/175), [universal VST/PDC #95](https://github.com/masarray/arsonmix/issues/95) and [VST-equipped Show #180](https://github.com/masarray/arsonmix/issues/180) are NOT yet complete. Preserve [L2 GUI lock #176](https://github.com/masarray/arsonmix/issues/176).

**Benchmark & delivery roadmap (2026-10-30):** [eMotion LV1 + Studio One/Fender Studio Pro 8.2 parity plan](docs/ROADMAP_LV1_STUDIO_PRO_MIXER_PARITY_20261030.md) maps actual code versus missing audio functionality, milestones MS0–MS7, dependencies and real-hardware FAT. [Epic #170](https://github.com/masarray/arsonmix/issues/170); [MS1 #173](https://github.com/masarray/arsonmix/issues/173), [MS2 #174](https://github.com/masarray/arsonmix/issues/174), [MS3 #175](https://github.com/masarray/arsonmix/issues/175), [MS4 #95](https://github.com/masarray/arsonmix/issues/95) + G5A [PR #178](https://github.com/masarray/arsonmix/pull/178) *in-flight at audit*, [MS5 #179](https://github.com/masarray/arsonmix/issues/179), [MS6 #180](https://github.com/masarray/arsonmix/issues/180). #15 actual 0FX crackle remains release-blocking; no CI claim equals hardware FAT.

**CURRENT R6 ARCHITECTURE / IMPLEMENTATION STATUS (2026-10-30):** [source-grounded current-state audit and executable roadmap](docs/R6_CURRENT_STATE_ARCHITECTURE_EXECUTION_20261030.md) + [premium LV1/Studio One operator UX contract](docs/R6_OPERATOR_WORKFLOW_CONTRACT_20261030.md). R6 supersedes historical G1/G2 next-step descriptions, not delivered L2 code. Protect [owner-accepted L2 #176](https://github.com/masarray/arsonmix/issues/176), 11 identical fader tracks, left-rail FX/ROUTE/SENDS, top MIXER/FX RACK/PATCH. G3 is merged. Next MS1 [#173](https://github.com/masarray/arsonmix/issues/173) independent native Mono/Stereo, MS2 [#174](https://github.com/masarray/arsonmix/issues/174) typed bus, MS3 [#175](https://github.com/masarray/arsonmix/issues/175) physical output, MS4 #95 plugin/PDC, with parallel #15 zero-VST hardware investigation. Never advertise planned FX/outputs as LIVE.


**Current GUI L2 baseline (MERGED 2026-10-30):** [PR #171](https://github.com/masarray/arsonmix/pull/171) / `2ddd434e` is now in `main`. All CH1–10 and MASTER share one 7-row responsive vertical strip grid; faders/meters/calibrated rulers resize together across FX/ROUTE/SENDS. FX/ROUTE/SENDS modes and daily utilities use the left rail; top segmented workspace tabs are MIXER, FX RACK, PATCH. G3 [PR #168](https://github.com/masarray/arsonmix/pull/168) native AUX sends are already merged, and L2 preserves that floating inspector. **DO NOT redesign/overwrite this operator-approved GUI**: [Issue #176 permanent regression contract](https://github.com/masarray/arsonmix/issues/176), [L2 design/QA](docs/L2_MIXER_CONSOLE_GRID_20261030.md), Chromium [PASS](https://github.com/masarray/arsonmix/actions/runs/40976822422) + Windows [PASS](https://github.com/masarray/arsonmix/actions/runs/40976822381). Exact 11-strip geometry tests run in CI. Independent channel/bus mono/stereo format (ADR R5), custom audio device rate/buffer, physical multi-output, and real ASIO hardware quality are **separate unfinished milestones**.

**R5 milestone issues:** [MS1 #173](https://github.com/masarray/arsonmix/issues/173), [MS2 #174](https://github.com/masarray/arsonmix/issues/174), [MS3 #175](https://github.com/masarray/arsonmix/issues/175); [product epic #170](https://github.com/masarray/arsonmix/issues/170). #143 is CLOSED historical routing GUI, not physical multi-output delivery.


**October 30, 2026 engineering status:** ArSonMix is an experimental Windows-first **10 input channel + 4 stereo AUX return + MASTER** mixer with real device-bound audio routing and VST3, not a hardware-qualified live/broadcast release. The native hardware 0-VST crackle issue [#15](https://github.com/masarray/arsonmix/issues/15) remains open. **Never interpret successful Windows CTest, GUI screenshots, or a workflow named Hardware FAT as proof of clean real-device audio.**

The [approved GUI visual baseline](docs/GUI_BASELINE_LOCK_2026-10-25.md) remains the reference for compact FX racks, BTN-180 and layout; its historical channel count is superseded by the current mixer PRD.

**Product/GUI research (2026-10-30):** [LV1 / Studio One / FL workflow benchmark](docs/GUI_WORKFLOW_BENCHMARK_20261030.md) and [YouTube customer voice -> verified feature requirements](docs/CUSTOMER_VOICE_REQUIREMENTS_20261030.md) are normative companions to the PRD/ARD. They define interaction acceptance and prioritization, not delivered native capability. Third-party reference screenshots are not redistributed.

**Mono/Stereo native roadmap (ADR R5, 2026-10-30):** [Independent source/channel/bus format architecture](docs/ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md) and [cross-thread execution handoff](docs/EXECUTION_HANDOFF_MONO_STEREO_REFORM_20261030.md) are mandatory before altering input modes, FX, send routing or hardware I/O. G3 native AUX PRE/POST editor is now merged on reviewed main@890cc310; the format flip/mono buses, PDC and independent physical outputs are not yet implemented. See [product epic #170](https://github.com/masarray/arsonmix/issues/170).

## Start here — new canonical product and engineering plans
1. [PRD v2.0 — Digital Mixer, per-channel FX/ROUTE/SENDS, custom hardware I/O, AUX buses](docs/PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md)
2. [ARD v2.0 — one audio graph, physical output patch, bus model, native ACK, staged execution](docs/ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md)
3. [Next-thread execution contract](docs/EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md)
4. [FX Rack / Routing cross-thread regression guard](docs/FX_RACK_ROUTING_CROSS_THREAD_CONTRACT_20261030.md)
5. [Smart Audio Engine safety architecture](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), [implementation blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md), and [real hardware FAT runbook](docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md)

The user-provided eMotion LV1, Studio One, Audition/DAW and FL-style wire references define the desired **workflow**, not an instruction to clone their artwork. Primary UX: stable MIX surface; FX/ROUTE/SENDS mode toggle, separate FX Rack and Routing workspaces, real INPUT/OUTPUT visibility in main window, native mono/stereo device matrix and proper AUX return processing. **Do not make Routing replace a channel's working VST3 inserts.** Do not expose raw JUCE audio configuration as the end-user product experience: keep JUCE audio backend but build an original capability-aware ArSonMix I/O screen.

## G1/G2 merged operator GUI (PR #167; hardware FAT pending)
The mixer FX/ROUTE/SENDS bank and native source picker use one existing device-bound routing controller.
Custom ArSonMix INPUTS/OUTPUTS/DEVICE panels include real input mapping and read-only MAIN L/R.
Hardware driver, buffer and sample-rate changes still delegate to the existing verified JUCE device
selector; fully custom hardware setup and physical output patch remain G2/G4 work.
These additions are experimental and require Windows package verification and physical acceptance;
**not an approved live-sound release**.

## Correct current feature boundary
- **Actual engine foundation:** ten native input lanes, four dedicated stereo AUX FX returns, bounded PRE/POST CH→CH and CH→AUX sends (32 edges), verified enabled ASIO/WASAPI DIRECT source pins, epoch/revisioned route ACK and safe muted/unpatched startup. These AUX returns are **internal mix paths**, not independently addressable physical headphone/output jacks.
- **VST3:** CH1 has an existing working **multi-insert native vendor VST3 chain** with editor, reorder, bypass and Orbit for eligible DearVR plugins; the routed callback still processes it. Other routed input/AUX lanes currently support one native effect per lane/return. Their additional FX rows must not be advertised as eight active inserts before actual multichannel expansion.
- **GUI:** MIX, FX RACK, ROUTING, SETUP and Show exist as evolving surfaces, with approved tactile metal faders, native meters and shared BTN-180 commands. The FX/ROUTE/SENDS bank and INPUTS/OUTPUTS/DEVICE overlay are now part of merged #167, but custom native mode/rate/buffer setup, physical multi-output patch and hardware FAT remain unfinished.
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
