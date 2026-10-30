# ArSonMix — Roadmap menuju kelas eMotion LV1 + Studio One / Fender Studio Pro

**NEW FACTORY FX REQUIREMENT (2026-10-30):** [ArSonMix Factory FX PRD](PRD_FACTORY_FX_20261030.md), [first-party VST3 distribution/architecture ADR](ADR_FACTORY_FX_BUNDLE_20261030.md), [manifest schema](factoryfx-release-manifest.schema.json), and [Factory FX epic #185](https://github.com/masarray/arsonmix/issues/185). Six bundled effects required: ArSonEQ, ArSonComp, ArSonVerb, ArSonHarmony, ArSonColor, ArSonKuPik Enhancer. FXF1 qualifies existing ArSonEQ feature-branch candidate + published ASKPVST binary and builds Factory registry/packaging/picker; FXF2 completes Reverb/Comp/Harmony/Color; FXF3 updates/portability/hardening. **No binaries shipped yet.** Keep MS0–MS7 milestones: Factory FX is a cross-cutting original-plugin delivery stream under MS4 FX plus MS6 portable Shows, NOT a second competing DSP roadmap. Preserve merged MS1 #184 and G5A #178, plus #15, L2 #176, independent user VST Vault #85.

> **ACTUAL MERGED DELTA / RECONCILIATION 2026-10-30 (supersedes audit rows below):** [G5A #178](https://github.com/masarray/arsonmix/pull/178) is now **MERGED** (internal eight-slot FX graph foundation, not user G5B multi-insert), and [MS1 #184](https://github.com/masarray/arsonmix/pull/184) is **MERGED** as `961c286d0f12e0f6223bb52251e67580922e4ca5`. MS1 adds native typed v2 physical source/processing channel format separation, MIX/PATCH GUI, preserved L2 geometry and actual DRY Show V4 channel format capture/recall. Exact-head MS1 [Chromium 40984090092](https://github.com/masarray/arsonmix/actions/runs/40984090092) and [Windows native/CTest 40984090111](https://github.com/masarray/arsonmix/actions/runs/40984090111) both SUCCESS. The source audit table below was recorded **before these PRs merged** and remains useful for its other constraints, but future threads must NOT recreate MS1 or G5A. Next [MS2 #174](https://github.com/masarray/arsonmix/issues/174) typed mono/stereo AUX/GROUP and [MS3 #175](https://github.com/masarray/arsonmix/issues/175) real physical output patch; G5B user-facing inserts/PDC (#95), full VST Show (#180), and physical #15 0FX hardware FAT remain pending. Recheck latest main and post-merge integrated CI before claiming combined qualification.

**Tanggal audit:** 2026-10-30
**Scope:** Live digital mixer / native Windows VST3 performance host; *functional workflow parity pada kelas 10 input*, **BUKAN** clone eMotion LV1/SoundGrid atau full DAW arranger.
**Repo status at roadmap reconciliation:** audited main `d8d70a6c5e489cfe1c915e6afbf6111973220ec1` already contains docs-only [R6 #181](https://github.com/masarray/arsonmix/issues/181) and operator architecture; review latest main again before coding. **Concurrent PR:** [G5A #178](https://github.com/masarray/arsonmix/pull/178) remained OPEN/DRAFT at verification (head `8d48542402ff55ecebfae7d900265e791ccbea3f`), **NOT MERGED / NOT FULL COMMAND+ACK IMPLEMENTATION**. Existing [product epic #170](https://github.com/masarray/arsonmix/issues/170) owns the product plan, [R6 architecture/current state](R6_CURRENT_STATE_ARCHITECTURE_EXECUTION_20261030.md) and [R6 operator workflow](R6_OPERATOR_WORKFLOW_CONTRACT_20261030.md) remain canonical implementation constraints (not competing roadmaps), and [L2 lock #176](https://github.com/masarray/arsonmix/issues/176) protects the owner-accepted GUI.
**Truth labels:** MERGED/CODE-OBSERVED ≠ exact-head CI GREEN ≠ real HARDWARE FAT PASSED. This is planning, not a delivered feature list.

## 1. Benchmark berbasis dokumen resmi — bukan visual clone
### Waves eMotion LV1 / LV1 Classic: operasi live
- [Waves LV1 Classic official user guide](https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html): MIXER layers vs strip MODE are independent; selected CHANNEL window covers input → rack → dynamics/EQ → Aux sends → output; special GROUP, FX AUX, MON AUX, MAIN, MATRIX, Cue/Talkback, DCA control-only; physical PATCH, SHOW/scenes, recall scope and recall-safe; optional Sends-on-Faders / Flip, Custom Layers, Spill.
- [Waves Live Support 2026-07-14](https://www.waves.com/support/waves-live-common-questions-answered): aligned plugin/bus delay compensation, selectable alignment groups; latency tied to buffer, network/DSP and plugins. LV1's separate SoundGrid DSP/server architecture is NOT comparable 1:1 with ArSonMix's PC-hosted JUCE VST3; target measured host reliability, not fictitious equal latency.
- **Translate:** independent bank display (which node?), independent mode (which controls?), selected-channel operational focus, truthful native routing, safe Scene recall, monitoring and measurable latency.

### PreSonus Studio One → Fender Studio Pro
- **Factual naming:** Fender announced the Studio One Pro rebrand on **2026-01-13**: [Fender official](https://www.fender.com/newsroom/270270-fender-unveils-connected-music-ecosystem-with-fender-studio-pro-audio-interfaces-and-controllers/). Latest update at audit **Fender Studio Pro 8.2 (2026-10-20)**: [Fender official 8.2](https://www.fender.com/newsroom/273556-fender-studio-pro-8-2-expands-mixing-and-creative-production-tools/). Legacy Studio One images remain useful for logical I/O and bus workflows.
- [Fender Studio Pro current Console manual](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm): mono/stereo input/output, real FX Channels and Bus Channels distinct; inserts, sends/cue mixes, pan, main out, visibility and grouping, metering, copy/paste channel settings; dynamically selected controls and narrow/large console layout.
- [Fender Studio Pro Channel Overview](https://www.fender.com/pages/fender-studio-pro): all key selected-channel parameters in one focused view; plugin editing without a maze of windows.
- **Translate:** typed source/channel/bus/output, flexible logical I/O → actual hardware pins, FX send/return, channel overview, plugin replace, channel copy/paste, fast user workflow. Do **not** pursue MIDI editors, arrangement/timeline, stem AI, full recording DAW, video, Dolby Atmos or 64/80-channel claims in core milestones.

## 2. Current evidence-based ArSonMix status at audited `main`
| Domain | Observed / merged foundation | Missing, incomplete or NOT HARDWARE-VERIFIED |
|---|---|---|
| MIX console | [L2 #171 merged](https://github.com/masarray/arsonmix/pull/171): 10 input + MASTER, common 7-row fader/meter grid, left FX/ROUTE/SENDS rail, top MIXER/FX RACK/PATCH, native WebView2 and standalone parity. Chromium [40976822422](https://github.com/masarray/arsonmix/actions/runs/40976822422) + Windows [40976822381](https://github.com/masarray/arsonmix/actions/runs/40976822381) GREEN at L2 exact head. | Native operator CHANNEL focus, true AUX/GROUP layers, sends-on-faders, bus-focused view and tested iGPU pointer performance not completed. No unilateral MASTER resizing allowed [#176](https://github.com/masarray/arsonmix/issues/176). |
| Audio graph/routing | Actual one prepared 10-channel + 4 **stereo FX AUX return** graph; up to 32 sends; native physical input patch, device epoch/revision check, explicit muted apply/positive ACK, real CH1 legacy VST path integrated; G3 [#168 merged](https://github.com/masarray/arsonmix/pull/168) native PRE/POST send inspector. | Device-bound 0FX crackle [#15](https://github.com/masarray/arsonmix/issues/15) unresolved in real field hardware; no independent output-bus→physical pin routing; no full MON/CUE/GROUP/MATRIX/DCA. |
| MONO/STEREO | `engine/core/ChannelRoutingContract.h` models format and fold; GUI has source physical mono/stereo input choice. | `engine/core/NativeDryRouteCommand.h` still sets processing channel format based on source mode and **BUS=stereo**. No independently switchable, fully live channel/bus format [#173](https://github.com/masarray/arsonmix/issues/173), [#174](https://github.com/masarray/arsonmix/issues/174). |
| VST3 FX | Native CH1 up to 8 slots; CH1 Orbit and prepared graph handoff; routed CH2–10 + four AUX each currently one *user-commanded* native insert. `src/PluginVault.h` has explicit copy-to-workspace VST3 import. | [G5A #178](https://github.com/masarray/arsonmix/pull/178) is an **open separate branch** adding real 8-slot internal prepared graph per lane, **NOT** yet a multi-insert GUI command/ACK. No universal 8 live FX per channel/master, no general mono/stereo VST bus negotiation or PDC [#95](https://github.com/masarray/arsonmix/issues/95). `src/LiveAudioEngine.h` and `src/LiveDryRoutingLab.h` explicitly reject nonzero FX latency in relevant paths until PDC. |
| SHOW / portability | Versioned DRY Show schema / capture, worker save, muted DRY recall foundations, Plugin Vault / PortableWorkspace / StudioBackup, snapshot and presets. | No proven complete all-channel FX instance/vendor binary state restore, scoped scene recall-safe, device remap/restore for physical multiout, or instant portable startup FAT on different PC. [#85](https://github.com/masarray/arsonmix/issues/85). |
| Monitoring | Native `engine/dsp/NativeMasterLoudness.h` and `tests/native_master_loudness_test.cpp` implement/test integrated LUFS-I and 4× reconstructed dBTP for **48 kHz only**, with invalid/unsupported semantics at 44.1 kHz. | Existing [#135](https://github.com/masarray/arsonmix/issues/135) wording claiming *no native engine* is HISTORICAL and must be updated after checking latest wired status. Need multi-rate calibration and actual field verification; do not report unmeasured dBTP. |
| Low-end GUI / release | Baseline #176 geometry CI, original PNG fader and BTN-180, filter-free shell; Windows CI package verification passed for L2. | Integrated-GPU pointer latency / UI frame time [#56](https://github.com/masarray/arsonmix/issues/56), actual 2×2/8×8 hardware FAT, portable plug-in vendor licensing constraints, driver reset and long soak require independent evidence. L2 PR Windows artifact upload was skipped, so don't present its workflow as downloadable ZIP. |

**Source confidence:** directly read `web/mixer-layout.css`, `engine/core/NativeDryRouteCommand.h`, `src/LiveGraphHandoff.h`, `src/LiveAudioEngine.h`, `src/LiveDryRoutingLab.h`, `src/PluginVault.h`, `src/StudioBackup.h`, `src/NativeShowSchema.h`, `engine/dsp/NativeMasterLoudness.h`, `tests/native_master_loudness_test.cpp`, and merged #171 metadata. For future work re-audit HEAD; docs and old issue bodies can lag behind code.

## 3. DELIVERY ROADMAP — significant end-to-end milestones, dependency-driven
**MS0 — P0 audio & visual reliability gate (PARALLEL ALWAYS; blocks release)**
Canonical [#15](https://github.com/masarray/arsonmix/issues/15), [#56](https://github.com/masarray/arsonmix/issues/56), [#176](https://github.com/masarray/arsonmix/issues/176).
- Capture physical ASIO/WASAPI device and clock evidence for 0 FX at 44.1/48 kHz, 128/256/480 frames as supported; measure capture discontinuity, render starvation, callback scheduling and clock mismatch *separately*. Bluetooth/virtual independent-clock paths get their own scenario; no false conclusions based on CPU callback time alone.
- Correct observed defect and measure continuity on actual 2×2 + representative independent-stream path; no auto-unmute/hotplug surprise. Gate native graph changes with 0FX A/B, steady output and epoch fail-closed.
- Low-end Windows 11 iGPU pointer/frame performance; avoid backdrop blur, expensive compositing, hover reflow, full DOM rebuild, redundant VU rendering.
- **Done:** hardware FAT report with device names/rate/buffer, audible test, native diagnostics, reproducible pass/fail + the Windows EXE/ZIP from exact-HEAD workflow. Do not mark PASS from CI screenshot alone.

**MS1 — independent native MONO↔STEREO source vs channel [#173](https://github.com/masarray/arsonmix/issues/173) (NEXT PRIMARY VERTICAL SLICE)**
- Versioned typed `SourceBinding` (NONE, physical L or L/R, BUS) and separate channel `processingFormat` on one existing route graph; preserve sparse/nonadjacent enabled pin identity and legacy Show migration.
- Real mono→stereo projection/pan; real stereo→mono once-only documented 0.5(L+R) fold; plugin layout reject/adapter off RT. No duplicate physical R.
- Selected channel M/L·R format badge and PATCH separation, MUTE→DRAFT→APPLY→positive ACK / negative rollback. Keep CH1 VSTs/Orbit and L2 fader geometry.
- **Demo:** MIC1 physical MONO → channel STEREO format (no phantom input), true stereo 2 pins → MONO, reversibility, device unplug, no sound change until accepted. CTest + Chromium + native ZIP.

**MS2 — native typed FX AUX and GROUP + mono/stereo-aware sends [#174](https://github.com/masarray/arsonmix/issues/174); parent [#109](https://github.com/masarray/arsonmix/issues/109)**
- Typed FX AUX vs GROUP vs MON/CUE vs MAIN identities with explicit source/send conversion, bounded pre/post gain/pan, one shared plugin instance per return, DAG/cycle and double-summing guards. Existing 4 FX AUX/32-edge cap stays unless benchmark/measurement supports a higher limit.
- Native GROUP mono/stereo as a real bus (not alias of FX AUX); inspector mirrors graph ACK and bus fader/meter; do not pretend FX returns are hardware jacks.
- **Demo:** two mono microphones feed one stereo AUX reverb VST; separately route both into GROUP, process once, correctly feed MAIN; in-phase/anti-phase and headroom tested. Real native chain/GUI/hardware evidence.

**MS3 — real hardware physical OUTPUT PATCH & independent monitor [#175](https://github.com/masarray/arsonmix/issues/175)**
- Prepared render-side `OutputPatchPlan`, stable enabled render pin identities/masks, separate logical bus→physical output ownership, mono/stereo patch transaction and device hotplug guards; only one callback.
- Original ArSonMix SETUP DEVICE / INPUTS / OUTPUTS grid uses genuine ASIO/WASAPI ports, logical M/L/R routing, no fake #3/#4 on 2-out device. SHOW rebind/restore must stay MUTED.
- **Demo:** 2×2 allows MAIN 1/2 and refuses MON 3/4; 8×8 routes MAIN→1/2 and MONITOR/CUE→3/4 simultaneously without bleed/double-sum, optional SUB→5 if real bus exists; measured separated hardware outputs.

**MS4 — universal native FX rack + latency alignment (G5A IN-FLIGHT; parent [#95](https://github.com/masarray/arsonmix/issues/95), [#178](https://github.com/masarray/arsonmix/pull/178))**
- Integrate G5A prepared eight-slot/channel foundation **only after exact-head Windows and Chromium GREEN**; do not call it complete while PR open. Then expose one channel/bus-aware FX command schema: Add/Replace/Edit/Bypass/Move/Remove, per-slot persistent vendor identity/state, safe reorder, correct plugin mono/stereo/sidechain bus layout.
- PDC for converging dry/wet and parallel paths: accumulate plugin/bus latency, bounded delay line per path, align to common render endpoint, detect dynamic latency updates, safe state transition. No unbounded RT delay allocation, mute/error policy for excess/unknown latency; don't silently disallow all useful live plugins forever.
- **Demo:** CH2 actual 3 serial VSTs, CH3 sends to shared AUX reverb, high-latency vs dry parallel impulse alignment/null evidence, drag/drop or plugin swap without crackle/vendor lifetime loss, MASTER no fake rack. CTest + native ZIP + real ASIO test.

**MS5 — operator-grade CHANNEL / MIX layers [#179](https://github.com/masarray/arsonmix/issues/179)**
- Selected CHANNEL focus (input→real inserts→sends→bus→physical out), compact bank select INPUT / FX AUX / GROUP / MAIN, selected-edge trace, exact ACK state, no fake built-in DSP.
- Safe Sends-on-Faders (Flip) only with explicit target and enter/exit: faders control send level rather than secretly altering main levels; original values restored. Later DCA/spill layers only after typed audio/control graph supports them.
- **Demo:** select MIC1, inspect full native path, edit send, choose AUX1 sends-on-faders and back without fader/meter jumping. 1366 iGPU, L2 #176 11-strip pixel geometry PASS.
- **Concurrency:** inspector can be designed early with existing source truth but physical output/Group controls remain disabled until MS2/MS3.

**MS6 — Show / Scene / Recall Safe + portable production [#180](https://github.com/masarray/arsonmix/issues/180), [#85](https://github.com/masarray/arsonmix/issues/85)**
- Full native show graph including versioned typed routing, all applicable VST vendor binary state, owned user VST vault manifests and stable hardware mapping, selective scope and Recall Safe, transactional recall/rollback.
- One public/private EXE feature set, no proprietary user VST distributed. Save/restore user-owned plugins/presets from USB to another PC with version/dependency check and rebind wizard; no global rescans.
- **Demo:** two scenes, Recall Safe on live vocal channel, VST preset/FX routing changes elsewhere, power/reopen, disconnected device restores muted with actionable negative ACK; can load backup on clean second PC where licensing permits.

**MS7 — product hardening, not DAW clone [#56](https://github.com/masarray/arsonmix/issues/56), [#37](https://github.com/masarray/arsonmix/issues/37), [#135](https://github.com/masarray/arsonmix/issues/135), [#85](https://github.com/masarray/arsonmix/issues/85)**
- Multi-rate native LUFS-I / 4× dBTP valid data or "unsupported", peak-hold / true colour-fixed meters, Audio/CPU/xrun telemetry and visible graph health. Plugin isolate/recovery where feasible; real device setup custom rate/buffer choices and fail-safe recovery; only tested hardware marks READY.
- Professional copy/paste channel settings, user labels/colors, hardware control/MIDI mappings, action history/undo, contextual tooltips and bounded banked 8/16/32 channels **after** actual capacity is measured. Safe audio file player/cue, remote/tablet, talkback, matrix and DCA become optional later typed nodes, **not** prerequisites for first useful production mixer.
- **Demo:** repeatable 2×2 and 8×8 on standard iGPU PC, lengthy soak, USB recovery and compact GUI user journey; release versioning and performance budget report. No promise of LV1 SoundGrid/AoIP compatibility or full Fender DAW.

## 4. Dependency graph / concurrent owners
```text
                      +-- MS0 #15 real hardware clock/audio gate (always parallel; release BLOCKER)
                      +-- #56 pointer/iGPU, #176 L2 pixel lock (always parallel)
MAIN L2 + G3 ---------+-- MS1 #173 source/channel typed native format
                       |       -> MS2 #174 typed sends + FX AUX/GROUP
                       |                  -> MS3 #175 physical mono/stereo output+MON
                       |                  -> MS4 #95 PDC and format-aware multi-inserts
                       +-- G5A PR #178 *open* multi-slot internal foundation -> MS4 G5B cmd/GUI/PDC
                       +-- MS5 #179 focused UI (stage now; enable only on real accepted native types)
                       +-- MS6 #180 native Show & Recall Safe (schema design parallel; full integration after MS1–4)
                       +-- MS7 UI speed, meter, portable QA and release gates
```
**Ownership contract:** single native graph/control owner at a time for `LiveAudioEngine`, `LiveDryRoutingLab`, `PreparedMixerBlock`, `NativeDryRouteCommand`, `LiveChannelFxGraph` + bridge codecs; schedule dependency PRs strictly. Independent CSS/docs/benchmark and CTest modules may proceed in parallel, but merge against newest exact main, no silent overwrites of #178 and no draft stale PR bulk merge. Every PR delivers one demonstrable vertical native+GUI+test slice; not a series of decorative buttons.

## 5. Acceptance matrix — practical end-to-end product demos
| Gate | Operator action | Required native/evidence |
|---|---|---|
| FAT-0 | launch on real Windows, 0FX MIC→MAIN | No audible pop/crackle at tested ASIO/WASAPI setups; input+output discontinuity evidence; output stays MUTED until explicit arm |
| FAT-1 | physical input MIC1 (mono), switch *channel format* M→STEREO; route another L/R source→mono channel | Correct independent source-format and channel-format PCM, no phantom pins, native ACK+rollback |
| FAT-2 | send MIC1 + MIC2 POST to one AUX1 stereo reverb, toggle PRE and group dry signals | One VST instance per AUX return, expected gain/pan/phase, accepted routing, no double MAIN |
| FAT-3 | 2-output and 8-output device, patch MAIN and MON independently | Real output pin readback, no fake columns, rejection safety, independent waveform on pins |
| FAT-4 | move/add/remove VST3 on CH2 and AUX, introduce latency-inducing VST | Real serial processing and preserved state, no callback detach, PDC wet/dry alignment, no click |
| FAT-5 | select channel, switch FX/ROUTE/SENDS, resize and select AUX bank/Send-Fader | MASTER/CH1–CH10 aligned, actual live graph state, no unexpected source fader jumps; iGPU responsive |
| FAT-6 | recall scoped Scene, restore missing-VST backup, hotplug device | Recall Safe, full rollback, no sudden unmute, honest missing asset/rebind prompt |

## 6. Workflow / engineering conventions
1. At start of every thread: verify `main` SHA, merged PRs, open #178/#173/#174/#175/#95, read `AGENTS.md`, `ADR_R5...`, L2 #176 and this roadmap; don't trust old handoff snapshots.
2. Use **one typed native route graph**; off-thread control prepare; immutable published graph; bounded, buffer-boundary ACK; no malloc, locks, plugin create/state, JS/JSON, filesystem or callbacks detach inside audio RT.
3. Native source of truth `DRAFT → MUTED APPLY → ACCEPTED ACK → LIVE` (never auto-unmute). Plugin FX lifecycle and Show changes share the same safety gates; GUI navigation never writes audio.
4. Minimize CI expense: local/static/contract tests first, batch coherent changes, exact-head Chromium + Windows native/CTest/package for changed C++; docs-only commits don't trigger needless manual Windows jobs. Avoid CI until source and tests internally consistent; don't infer FAT from CI.
5. **Evidence log** for each milestone: exact commit, PR, test run, screenshot at 1366×768 / 1440×768 / 1680×920, Windows artifact upload if needed, real-device/rate/buffer and negative-ACK rollback evidence; statuses CODE / CI / HARDWARE kept separate.
6. Preserve one public ArSonMix application with user-owned private VST/presets; don't ship third-party copyrighted assets. **Release is blocked by #15 until actual hardware shows stable audio.**

## 7. Immediate next integration decisions
- **First:** preserve already merged L2 #171 and status #176. Continue MS0 0FX physical triage while MS1 #173 starts on latest main; do not add new cosmetic routing controls.
- **In parallel:** review and gate [G5A PR #178](https://github.com/masarray/arsonmix/pull/178) (one real multi-slot foundation), but never turn on fake extra slots or touch current G3/native routing command before coordinating with MS1/2. Maintain one owner of shared graph.
- **Next releases:** MS1 source/channel independent; MS2 real bus types and sends; MS3 verified outputs; MS4 full multi-FX/PDC, MS5 selected channel, MS6 scoped native Show and portable backup. Integration may reorder pure UI studies, but not native correctness dependencies.
- **Avoid distractors:** full DAW arranging, SoundGrid server protocol, copied LV1 graphics, instantly increasing channels to 64, all new plugins bundled, GUI-only PDC/matrix, or an installer without working hardware.
