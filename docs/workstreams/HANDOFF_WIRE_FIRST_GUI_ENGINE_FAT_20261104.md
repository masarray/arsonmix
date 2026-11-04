# EXECUTION HANDOFF — WIRE-FIRST RELEASE / ONE REAL MIXER (2026-11-04)

**READ FIRST, DO WORK.** Product [PRD](../PRD_WIRE_FIRST_LIVE_ROUTING_20261104.md) / Architecture [ARD](../ARD_WIRE_FIRST_ZERO_ADDED_LATENCY_20261104.md) / native [v8 console contract](../../contracts/console-puzzle-v8.json) / root [AGENTS.md](../../AGENTS.md). This packet is a **prioritized execution handoff**, not claim of working engine, tested sound, published screenshot or passed CI. **Do not open another roadmap issue/clone engine; use current owners.**

## 0. Source-of-truth recovery procedure before ANY code edit
1. Read GitHub main SHA, issue #351 (GUI), issue #192 (native dynamic graph/ABI), issue #15 (0-FX physical crackle); inspect PR #266 ENGINE, #271 GUI, #270 FAT, #278 approved design, #352 WIRE direct controls, #360 V52 fader, #361 FX rail; latest checks and conflicts. This document sampled main **0215986fbbe2c67971680952f75f069950ac653e**, not a promise latest tomorrow.
2. Read approved checkpoint docs/CHECKPOINT_GUI_M1_WIRE_V48_BUTTONS_V23_20261103.md and current V51 meter checkpoint; do not edit accepted meter/fader CSS while PR #360 runs.
3. Discover existing code/tests; grep reusable functions before writing new code. Use *one writer per HTML/CSS owner*. #352 is draft/diverged; cherry-pick/reconcile only passing deltas onto fresh main, not blind force merge.
4. Work in meaningful integrated milestones below. Keep UI/ENGINE/FAT in owner PRs; use one controlled integration SHA, same native C++ fixture, no separate fake backend.
5. If GUI pages looks finished but native callback isn't installed, label it GUI SIMULATION. If native PCM can't be observed, label NOT INTEGRATED. No user UAT until coherent Windows product.

## 1. Current repo and responsibility map (verified 2026-11-04)

| Owner | Existing work / state | Paths and duties |
| --- | --- | --- |
| Reference GUI #351, #278, #352, #346/#347/#349 | Approved visual; direct cable/physics #352 draft, conflicts; #360 fader V52 draft | site/preview-v2/index.html, site/preview-v2/assets/mixer-wire-dots.css and current WIRE CSS owners; browser tests; NEVER overwrite V51 meters or V52 fader. |
| GUI NATIVE #271 | Draft, does not prove real WIRE to callback | web/**, generated standalone GUI, GUI producer-fixture consumption / snapshot+ACK / capability binding |
| ENGINE #266, issue #192 | Draft sole runtime owner; ChannelId prepared/offline, no full callback cutover, current live M4 10+4 AUX | src/**, engine/core/**, native VST3 lease / one callback / PCM / accepted graph, request handler |
| FAT #270 | Draft exact-SHA harness | contracts/** (coordinated), native fixture->GUI replay validator, CI/test/packaging / physical evidence |
| Physical audio blocker #15 | OPEN; crackling reported with **zero FX** | device/capture/render/discontinuity root cause, independent output/input/dry tests. Never infer audio clean from low callback CPU. |
| GUI FX #361 | Separate approved refinement task, not route owner | Preserve inline FX rack and native stable VST3 state; never replace FX with a router shortcut. |

**Why this change:** legacy browser routing currently has four representations (wireEdges, track.output, sendSlots, input split) and no MAIN cable in wireDisplayRoutes; #wireLayer exists inside #viewport while #master is outside, master incoming drag is filtered, MAIN click/drop may be blocked. This is source-observed, not hypothetical. Do not fix by CSS fake wires alone.

## 2. Release priority / non-goals
**P0:** one universal channel, valid physical/source patch, real VST3 inserts, user-flexible internal WIRE and default CH->MAIN. Can have CH1+CH2->CH5->MAIN, and CH1->CH6 (insert reverb) ->MAIN plus dry CH1->MAIN. A channel receiving WIRE and physical input sums the permitted sources correctly. **No separate BUS or FX DSP owner**. Dedicated +BUS/+FX RETURN creation and role-specific workflow **deferred** behind feature flags; do not delete legacy AUX/Show or block compatibility. Sidechain and multi-tap same source/destination future opt-in.
**UX:** no route badges, descriptions, boxes, decorative buttons, extra panels or full cross-console MAIN cable. Preserve very dense graphite visual and existing route controls. Only physical MAIN-colored hanging source stub plus matching upward main plug; accepted state shown physically.

## 3. M1 — CANONICAL GUI REFERENCE, MAIN PLUG AND GUARDIAN (one meaningful GUI deliverable)
**Owner:** GUI reference #351/#352; coordinate HTML owner with FX #361 and fader #360.

M1.1 — Normalize GUI to one state reducer:
- Define a typed channel/edge store (ChannelId/edgeId strings; mainId explicitly one of channels; POST_FADER default, gain, enabled).
- Derive WIRE svg, MAIN stub, channel OUT, navigator, matrix, plugged/unplugged socket state and route counts from ONE computed snapshot; remove independent mutations to track.output/sendSlots. A compatibility import with explicit duplicate/tap conflict resolution handles saved previews/scenes; no silent route deletion.
- Every existing action calls one reducer intent (add/remove/retarget edge, route-only batch, gain, selection). Selection/bank/drag may be transient; audio topology never changes from hover or view change.
- Add default MAIN edge for each newly created ordinary channel. Existing restored deliberate MAIN disconnections stay disconnected. Do NOT create two sends when source.output==Master and wireEdge already exists.
- Existing browser reference can run reducer locally but never label it LIVE. No WebAudio/audio engine in JS.

M1.2 — Physical MAIN cable and socket interaction:
- Render per-connected-channel short MAIN-palette-colored sheath dropping from OUT into chassis, and pinned MAIN sheath rising from below into IN; paired stubs mirror same edgeId. One master socket sums N sources but gesture operates only chosen source edge. Existing color/material/shading and hardware plug artwork only. No full-span source->MAIN line, no text/status badges/cards.
- Fix current #viewport/#master sibling geometry with one measured coordinator and small overlays/painter, using actual DOM jack center rectangles; preserve bank geometry, scroll, collapsed 23px, pinned MAIN width and FX/fader regions. No absolute x guesses or extra padding panels.
- Integrate source->MAIN in canonical wireDisplayRoutes; main receiving socket in click and existing pointer capture and drag selection (currently master is skipped). Pull MAIN plug >~10px, physically follow cursor, release in valid empty WIRE bay disconnect selected edge; can drag plug back to MAIN IN; invalid/cancel/Escape restores; reconnect with magnetic snap. Do not confuse click with detach. No change to unrelated channel WIREs. Stable selected edge gain/mode/Undo.
- On no unique selected MAIN source do not disconnect all: use existing navigator/matrix selection. Click connected source OUT to MAIN again must not auto-disconnect due to drag-drop collision.
- One transient cable spring per gesture, obey reduced motion, no idle RAF or full DOM remount on pointermove.

M1.3 — Guardian integration and adversarial GUI tests:
- One authoritative full-candidate DAG validator for every source of edges: WIRE, channel OUT, legacy send, old PRE-FX split, Send-on-Faders, Scene/SAFE and Copy/Paste.
- Cycle/duplicate/invalid/revision blocked before candidate commit. WARN dry reconvergence/high latency, DO NOT auto-change signal. Route-only is single atomic batch preserving unrelated sends. The existing demo-only route-choice UI should not be replaced by another modal forest.
- Fast tests should assert no duplicate route after source-WIRE then Send-on-Faders, no hidden sends, no cycle via each mixed path, no MAIN global disconnect, same snapshot across all editors, undo of exact accepted revision.
- Borrow existing tests/pages_wire_v49_direct_controls_smoke.py, tests/preview_v15_drag_wire_smoke.py, tests/pages_wire_v46_precision_smoke.py, tests/pages_v50_wire_popout_layout_smoke.py; add dedicated MAIN/equivalence/graph-model/property tests. Fix tests when incorrect, never weaken a real regression to turn CI green.

**M1 exit:** source HEAD + screenshot proof across 1024/1366/1680/1920 (Comfort/Standard/Compact, zoom 100-150%), one- and multi-edge MAIN plug interactivity, batch/cancel/Undo, no FX/rail/fader/meter drift, JS test suites and exact branch CI. This is GUI-only, no native PCM certification.

## 4. M2 — ONE NATIVE WIRE GRAPH + ACTUAL VST3/PCM (engine + GUI slice, NOT a new engine)
**Owner ENGINE #266 and GUI native #271 using same v8 socket; FAT #270 validates concurrently.**

M2.1 — Contract alignment:
- Before implementing WIRE op, inspect real C++ producer and existing ConsoleIntent operation inventory. Propose additive APPLY_WIRE_TRANSACTION for graph batch connect/remove/retarget and corresponding accepted edge inventory, using v8 schemaVersion=6 base envelope. Do not invent JS-only operation. Add supportedOps only after installed handler+callback. The 3 owners share exact producer fixture and validator; if old SET_PRIMARY_OUT/UPSERT_SEND can be safely adapted, centralize both in one graph transaction.
- Response ACK matches requestId+epoch+revision; GUI switches cable to committed only on matching snapshot, rejection restores physical plug exactly. Pending never presented as live sound.
- Native channel creation atomically starts with one edge to MAIN; physical input selection is distinct, deliberately source-only channels remain silent at MAIN. Channel IDs stay stable on reorder/bank/rename; edge IDs remain stable through retarget. Existing M4 and Show must not be lost.

M2.2 — One engine callback:
- Reuse DynamicChannelRoster, DynamicPreparedMixerBlock, Native prepared PCM and RealtimeRoutePublisher, migrate CH1 special vendor lease/Orbit/FX state and existing CH2-10/4 AUX/Show BEFORE cutting over M4 (exact #266 owner mandate).
- Control thread compiles whole candidate DAG, topo schedule, preallocated buffer layout, mono/stereo conversion coefficients and latency metadata; callback executes in single device period, zero per-hop FIFO/worker, no allocations/locks/UI, only bounded atomics and prepared VST chain. No force-close or recreate VST3 editor simply because routing changed.
- Direct and nested 1/2/3-hop paths show identical onset sample at 0FX/0-plugin latency; fan-in/fan-out amplitudes agree with reference. MAIN source disconnected does not remove indirect CH->CH->MAIN audio. Safe param smoothing and graph handoff preserve audio (no unexpected global mute).
- Plugin PDC getLatencySamples and kLatencyChanged processed off audio thread. Live latency budget explicit; parallel reconvergence warns on unaligned latency; don't claim perfect time alignment if not implemented.
- Device epoch, output ownership and ARM safe; physical port labels from backend, no phantom stereo or invented device.

M2.3 — Native GUI projection:
- Native web/** consumes same accepted edges; preserve compact approved channel controls; no parallel track.output graph or simulated meters. Same WIRE and MAIN drag intentions render exact state from real snapshot. If operation unsupported, use honest disabled physical capability, not decorative pretending to process audio.
- Real VST3 on *different* channels, open native editors, save vendor state and reload. GUI-only plugin manager metadata never qualifies as native scan.

**M2 exit:** end-to-end Windows callback and GUI at one integrated commit: verified physical input->CH07->MAIN, two independently FX-processed channels, CH1->CH3->MAIN chain, CH1 direct MAIN disconnect/reconnect while CH3 indirect path persists, simultaneous fan-in/fan-out, 1 click/drag controls, true native PCM and meter, Show restored. No CH11 claim unless independent accepted native channel actually works.

## 5. M3 — PERFORMANCE / FAT / RELEASE GATE
**Owner FAT #270, ENG #266 and GUI #271 fix failing defects in their lanes.**

- Run actual native-generated fixture -> GUI consumer -> contract validation; no hand-crafted incompatible mock. Validate accepted snapshots and rejected stale intents. Package one exact-SHA signed/provenance Windows build.
- 0FX physical isolation #15: output-only test tone, capture-only counters, dry-through; compare same ASIO device vs WASAPI shared/exclusive and virtual/BT endpoints. Capture/output driver clocks, samples/period, discontinuities, *real* output starvation when detectable, drift, buffer behavior. Callback gap alone != proven xrun. Root-cause and retest rather than hiding crackle via buffer enlargement without disclosed latency consequence.
- Matrix profiles: 44.1/48/96k where supported; buffer 64/128/256/512; one pin MONO/independent L-R; 16/32/64 admitted channels; 0/2/N VST3; direct/nested/parallel route; one-hour dry and full-FX, rapid route edits/undo/Show and forced plugin/driver faults. Record CPU p50/p95/p99/max callback time as fraction of period, xruns, memory, true loopback latency and UI responsiveness. First headroom target below 50% of period for typical supported normal-load case, NOT false LV1 comparison.
- No user UAT until product-complete internal package and repeatable audio qualification; if no physical rig, mark FAT PENDING and stop release claim. Separate Windows/Chromium-green does not suffice. #15 remains hard blocker.

## 6. Agent acceptance / restart ledger (each owner posts one meaningful milestone)
Each completion report should contain:
- Owner and exact PR branch/HEAD, affected files, current integration SHA, base SHA.
- Contract schema version + **C++ native-produced fixture hash** and supportedOps, exact command and actual result.
- Concrete operator journey now *audibly* supported (or GUI SIMULATION / NOT INTEGRATED); 0FX/loopback hardware state.
- Failures with reproduction and root cause, one corrective action in owning lane; no percentages, vanity progress, screenshots called PCM, unknown future work.
- Single next integrated vertical-slice acceptance condition.

**Preflight commands to discover/execute, only when actually supported on working tree:**
~~~bash
node scripts/check_console_puzzle.mjs
node scripts/check_console_puzzle.mjs --release
node --test tests/live_graph_contract_test.mjs
node --test tests/live_routing_ui_contract_test.mjs
python tests/pages_wire_v49_direct_controls_smoke.py
python tests/preview_v15_drag_wire_smoke.py
# native build/CTest/Playwright + Windows physical test according to existing CMake/CI owner;
# never claim these passed without an actual run on exact SHA.
~~~

**Do not waste time:** no parallel new graph builders, no generic rewrite to AudioProcessorGraph without demonstrated need, no callback-scanning watchdog, no extra CSS overrides, no handcrafted demo plugin/PCM, no new planning-issue proliferation, no premature full-PDC engine if user wants first sound. Solve the GUI-visible MAIN patch and full audio graph in meaningful integrated milestones. Keep FX Rack and visuals loved by operator.

## 7. References and issue links
- [PRD WIRE](../PRD_WIRE_FIRST_LIVE_ROUTING_20261104.md), [ARD WIRE](../ARD_WIRE_FIRST_ZERO_ADDED_LATENCY_20261104.md)
- [Master GUI execution #351](https://github.com/masarray/arsonmix/issues/351), [native scope #192](https://github.com/masarray/arsonmix/issues/192), [physical crackle #15](https://github.com/masarray/arsonmix/issues/15)
- [ENGINE #266](https://github.com/masarray/arsonmix/pull/266), [NATIVE GUI #271](https://github.com/masarray/arsonmix/pull/271), [FAT #270](https://github.com/masarray/arsonmix/pull/270)
- [approved design #278](https://github.com/masarray/arsonmix/pull/278), [WIRE #352](https://github.com/masarray/arsonmix/pull/352), [fader #360](https://github.com/masarray/arsonmix/pull/360), [FX/rail #361](https://github.com/masarray/arsonmix/issues/361)
- FL routing: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm
- Waves latency mode: https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html
- JUCE graph: https://docs.juce.com/master/classjuce_1_1AudioProcessorGraph.html
- Steinberg latency changes: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst.html

**END TARGET:** ONE finished native Windows mixer, one low-latency callback, user-flexible SAFE WIRE graph, physically intuitive MAIN cable, exact engine acknowledgment and real audio. This packet doesn't certify it exists today.
