> **FINAL OWNER CORRECTION (2026-11-04) — ONLY APPROVED GUI = https://masarray.github.io/arsonmix/preview/**. The recent reference to bare /preview-v2/ as native/product GUI was a mistake and is **REVOKED**. Preserve the EXACT /preview/ assembled visual: internal HTML/WIRE/fader/FX source **plus approved PNG hardware button CSS/JS/sprite**. Publish ONE /preview/ GUI; /preview-v2/, /preview-buttons/ and archive URLs redirect-only. Native JUCE engine must stitch into THIS renderer through a thin real accepted Snapshot/Intent/ACK adapter, not recreate UI. GUI #389, ENGINE #266/#387, FAT #270. [Single handoff](workstreams/HANDOFF_APPROVED_PREVIEW_V2_NATIVE_20261104.md) (filename historical, content now /preview/). All conflicting GUI descriptions below are superseded; retain realtime/ABI/PCM/device/VST3 safety requirements. Release requires exact compiled Windows GUI + PCM/FX/hardware FAT; Pages simulation is not audio evidence.

# ARD — WIRE-First Canonical Routing, MAIN Patch and Zero-Added-Latency Engine

**Date:** 2026-11-04. **Status:** normative target, not PCM completion evidence. Companion [PRD](PRD_WIRE_FIRST_LIVE_ROUTING_20261104.md) and [execution handoff](workstreams/HANDOFF_WIRE_FIRST_GUI_ENGINE_FAT_20261104.md). This overrides dedicated BUS/FX creation as a *first-release priority*, not the existing v8 ABI, native ownership, VST3 leases, Show/legacy AUX compatibility, approved GUI layout, or release/safety gates.

## A. Verified current-state inventory — main 0215986fbbe2c67971680952f75f069950ac653e

- GUI reference site/preview-v2/index.html is simulated. Current JS stores routes in wireEdges, track.output, sendSlots/activeSend and track.input split markers. wireDisplayRoutes excludes default CH->MAIN. wireIsCycle does not traverse all legacy sends. Sends-on-Faders and Copy/Paste/Scene can bypass unified cycle/duplicate validation. A screen that looks linked can differ from the actual intended signal graph.
- WIRE SVG #wireLayer sits inside #viewport (banks), whereas pinned #master is a sibling. Current geometry cannot represent a true MAIN endpoint; MAIN receiver specifically omits incoming WIRE edge handling in wireTray/pointerdown. requestWireConnection blocks some drops and toggleDirectMain mutates track.output independently.
- engine/core/DynamicChannelRoster.h has stable ChannelId and ChannelSendRef(primaryOutput). DynamicConsoleRoutingView.h is an accepted-session *projection*. DynamicPreparedMixerBlock.h is an offline prepared PCM kernel, NOT the installed callback. Current production remains 10 CH + 4 AUX/CH1 special VST path. ENGINE #266 is sole owner of native cutover; native GUI #389, FAT #270 are drafts; #15 physical 0FX crackle OPEN.
- contracts/console-puzzle-v8.json schemaVersion 6 already defines native requestId, deviceEpoch, baseGraphRevision, baseRosterRevision, operation, targetChannelId, ACK and accepted graph snapshot. Preserve that envelope. Do not invent a parallel browser-native protocol.

## B. Graph schema and ownership

One typed directed graph G=(V,E). All universal channels, including MAIN as sink, use stable ChannelId (decimal uint64 serialized as canonical strings). Every audible route, including default CH->MAIN, is an edge with a stable edgeId. GUI channel index, name, bank, physical pin and role are never identity. Physical capture SourcePatch and physical render OutputPatch are separate from internal WIRE edges.

Proposed control-thread model (target, not deployed API):

~~~text
Channel { id, format: MONO|STEREO, verifiedSourcePatch, nativeFxInstanceIds,
          fader, pan, mute, solo, meteringTap }
AudioEdge { edgeId, from: ChannelId, to: ChannelId,
            tap: POST_FADER|PRE_FADER|PRE_FX,
            gainLinear: finite_nonnegative, enabled: bool, signalKind: AUDIO }
GraphSnapshot { deviceEpoch, graphRevision, rosterRevision, mainChannelId,
                channels, edges, installedCapabilities, pluginLatency, outputPatch }
~~~

MVP policy: exactly **one enabled audible edge per source/destination pair** (not one per channel), POST_FADER default, no self or cycle, MAIN terminal and one summing input for N sources. Keep edgeId separate from pair key to permit future explicit multi-tap/sidechain expansion. Route-only is an **atomic macro** to connect source->target and remove source->MAIN; other sends stay. No duplicate physical PCM due to compatibility primaryOutput or legacy sends. Source-only has zero MAIN edges and remains capable of sending elsewhere.

CREATE_CHANNEL accepted transaction installs CH plus one unity CH->MAIN edge. An unpatched channel is silent but connected logically. A saved Show intentionally disconnected from MAIN must stay disconnected; do not default-repair it during restore. Legacy .output==Master and sendSlots must be migrated into one graph once, with conflict detection rather than silent audio duplication.

**Ownership domains:**
1. Native/control plane: unique canonical graph, native plugin leases, true device inventory, supportedOps/epoch/revision, accepted snapshots.
2. GUI: transient channel selection, focused source/edge, hover, drag, cable physics, PENDING; never declares accepted LIVE independently.
3. Routing Guardian: deterministic control-plane intent validator and graph compiler, event-driven.
4. Audio callback: immutable accepted topological schedule, preallocated PCM scratch, bounded realtime parameter atomics. No JS/WebAudio routing engine.

## C. Native Console v8 intent/ACK integration

Reuse arMixConsoleSubscribe, arMixConsoleSnapshot, arMixConsoleIntent, arMixConsoleAck; preserve string IDs and session-unique requestId. ACK accepted is *not* proof of audible LIVE until matching revision/epoch accepted snapshot from installed callback. All capabilities fail closed if unsupported.

**Proposed additive operation APPLY_WIRE_TRANSACTION** (not yet supported; agree with owner #266/#271/#270 and issue #192 before shipping). Reuses v8 intent envelope:

~~~json
{
  "requestId":"session:63",
  "deviceEpoch":"7", "baseGraphRevision":"42", "baseRosterRevision":"11",
  "operation":"APPLY_WIRE_TRANSACTION", "targetChannelId":"1003",
  "payload":{
    "reason":"ROUTE_ONLY",
    "changes":[
      {"op":"REMOVE","edgeId":"7201"},
      {"op":"CONNECT","from":"1003","to":"2000","tap":"POST_FADER","gainLinear":1.0}
    ]
  }
}
~~~

IDs above are illustrative. Atomic CONNECT/REMOVE/RETARGET by stable edge ID; batches commit all-or-nothing. RETARGET preserves gain/tap/identity after validation. A drag out of MAIN removes only selected source->MAIN edge. Undo replays inverse of an **accepted** transaction with freshness check; no direct JS graph restore to native. Continuous fader/pan/mute/send gain use RT-safe parameter plane with no topology rebuild. Adapt legacy SET_PRIMARY_OUT and UPSERT_SEND to the same graph once, not to a separate DSP routing path. Additive changes to snapshot edge inventory and supportedOps require native-generated fixture + GUI consumer + FAT validator updated in one integration checkpoint. Never claim new operation installed before it really is.

## D. Guardian implementation and safety

Control thread maintains adjacency indexed by ChannelId, edgeId hash, native channel format/role, per-source outgoing and per-target incoming. Use full candidate DAG validation (Kahn topological sort or DFS, O(V+E)) for scene, batch, copy/paste, migration and retarget. On single connection can test target reaches source in candidate; do not prematurely micro-optimize into untested cache. MAIN has no audio outgoing edges. Candidate must include all legacy-converted and current edges to catch mixed WIRE/SEND/OUT/split loops. Validate before allocating/preparing, then recheck base epoch/revisions at publish.

- BLOCK: cycle/self, duplicate audible same-pair, MAIN feedback, invalid ChannelId or tap, NaN/Inf/negative gain, stale revision, unsupported operation, invalid physical endpoint or mono/stereo mapping, memory/CPU admission exceeded, illegal plugin I/O, unsafe output-arm transition.
- WARN (operator decides): two dry routes reconverge (+6.0206 dB if signals identical at 0 dB), clipping/headroom, high plugin latency, no path to armed output, PRE-fader mute implication. Do not auto-mute, change wet/dry, pan or rewire.
- ALLOW: N-way fan-in/fan-out, legal nested routing, unpatched or deliberately silent source-only, FX insert on ordinary universal channel.
- External virtual-cable/physical/acoustic feedback cannot be proven absent by internal DAG alone; separate input/output patch warnings, physical output mute, limiter/clip indication and endpoint policy required.

Detect reconvergence efficiently via DAG reachability/ancestors or bounded merge-node check, not exponential enumeration of all source->sink paths. Route validation runs on structural events, NOT every audio callback or animation frame. Audio observer consumes decimated nonblocking telemetry (actual capture discontinuity vs output starvation where available, plugin exception/fault, callback budget, xrun counters, peak), not continuous graph rescans. Emergency output mute must work without control worker/GUI response.

## E. Realtime PCM graph — zero added samples, measurable not slogan

Build on existing DynamicChannelRoster, DynamicPreparedMixerBlock, RealtimeRoutePublisher and native leases. No second audio callback, new JS DSP or full replacement with generic AudioProcessorGraph. JUCE AudioProcessorGraph documents node routing, async/non-auto rebuild and processing cautions; use as proven pattern, but adopt existing repo's prepared runtime for ownership/lease correctness.

CONTROL (off callback):
1. Normalize all user intent into immutable candidate edges and verified SourcePatch/OutputPatch; validate IDs and graph DAG.
2. Topologically sort once; precompute edge gain/pan/mono-stereo coefficients, fan-in accumulator offsets, per-node source patches and send tap stages. Allocate bounded scratch for MAX ACCEPTED FRAMES and choose overflow behavior (fail-safe rather than RT malloc). No per-WIRE queue.
3. Prepare native plugin instances using actual supported I/O, sample rate, frame limit and declared latency; retain stable instance IDs on route-only changes. Reconcile existing CH1 vendor/Orbit/Show ownership before engine cutover.
4. Publish prepared graph at bounded callback boundary with proven lifetime/epoch reclamation. Do not create/destroy plug-in objects or free old graph on callback. If publish fails, old graph/PCM stays.
5. Callback once per device block: clear only needed preallocated accumulators, read verified capture frames, process each channel **once** in topological order, apply ordered inserts/taps/fader/pan/mute, accumulate edges to downstream buffers in the **same callback**, sum MAIN and map to verified output pins once. No worker thread hop, per-edge FIFO, per-hop buffer period, or synchronous locks.

**Zero-added-sample criterion:** with latency-free FX and aligned capture/render callback, direct source->MAIN and source->CH2->CH3->MAIN impulses arrive at the same *relative sample index*, regardless of number of legal WIRE hops. This is an internal processing assertion, not a 0ms microphone-to-speaker claim. Third-party VST3 can report nonzero getLatencySamples and kLatencyChanged; plugin reactivation and PDC rebuild run off RT. Automatic full PDC on parallel reconvergence may add audible monitoring delay: release LIVE default chooses bounded latency policy and warns/rejects oversized plugin delays, not silently delaying all monitors. Future per-output full PDC uses measured policy.

**Thread and memory invariants:** no heap, lock, UI, disk/logging, driver enumeration, JSON, VST3 scanning/opening/editor, graph DFS, implicit shared_ptr destructor or blocking call in processBlock. Parameter plane uses proven lock-free targets + smoothing. Stale controls are denied by epoch/revision. Support zero/variable-sized callback safely, out-of-bounds prevention and CPU budget. Xrun risk is measured, never guaranteed away for unknown drivers/plugins. Do not introduce threads solely to parallelize wires: profile first, as DSP parallelization can trade latency for throughput.

## F. Two physical MAIN cable fragments from ONE graph edge

Preserve approved console surface, MAIN pinned width, fader, meters, FX, theme, WIRE node and cable machinery.

1. For each connected source->MAIN edge, paint a short MAIN-colored, gravity-draped cable emerging from that channel's OUT and disappearing into the lower edge of the WIRE patch bay. When disconnected, omit that cable and expose an empty matching socket. No text/badge/card/CTA or full-width diagonal.
2. At pinned MAIN, paint one matching MAIN-colored cable emerging from below and entering MAIN IN. It represents **the focused exact source->MAIN edge**, or a noninteractive aggregate occupied state if multiple sources and no unique focus. Never imply one MAIN feed per geometric visible plug or allow aggregate disconnect.
3. Channel-to-channel wires keep source channel colors. MAIN cables derive color from existing MAIN channel/theme token, use the existing shared cable sheen/inner shading and metal plug cap. Realistic cubic sag, 0.5-2px restrained edge definition; no glow, endless ripple or shadow filters. Display may collapse to minimal cable stub at 23px compact WIRE, with zero strip geometry growth.
4. Existing #wireLayer is scoped to #viewport and MAIN is sibling #master. Use shared jack geometry coordinator with **two local overlay fragments** or a small parent-space overlay; read actual jack center rects, correct transform/scroll, clip to bay, keep pointer hit targets restricted to plug/socket. Do not use fixed pixels for x/y of MAIN or giant spanning path.
5. Refactor receiving socket gesture to include MAIN. Currently pointerdown ignores master in incomingWireEdges, while finish checks blankInBay only inside viewport. Resolve exactly one focused incoming edge by edgeId, give plug pointer capture and a >~10px deliberate drag threshold, same 22/24/31px snap zones as validated current WIRE, and accept detached end in a safe WIRE bay including MAIN strip. Escape/pointercancel/invalid release leaves topology+visual unchanged; drag back onto origin cancels. Commit on release only.
6. Drawing is projection of accepted graph; during drag it's a **transient visual candidate**. On GUI-only mock show simulated state explicitly in pre-existing status/footer (not new routing labels); native UI uses PENDING -> ACK -> matching accepted snapshot then committed cable. If ACK rejected, physically spring plug back and preserve accepted state.
7. Avoid idle RAF, DOM remount and geometry recalculation per pointermove; measure only on graph/layout/ResizeObserver/bank changes, use CSS transforms or affected SVG path update on active drag. Respect reduced motion, device pixel ratio and touch cancellation. One selected edge controls both visual fragments.

**MAIN socket and channel selection:** every channel initially MAIN-connected. Selecting source CH1 makes MAIN jack editable for CH1 edge only. If no source focused, click MAIN may inspect incoming routes via existing Navigator; never disconnect all. Dragging an *already connected* source OUT into MAIN should select its edge, not silently toggle away. Disconnect/reconnect leaves unrelated CH->CH sends intact. For a source with no direct MAIN, its MAIN-colored channel stub is gone even if audio reaches MAIN indirectly via another universal channel.

## G. Migration and compatibility

Implement one import adapter for legacy reference wireEdges/track.output/sendSlots/track.input. Map active legacy Master outputs to a single MAIN edge, send slots to identified edges and input splits to typed source taps or explicit graph edge depending on semantics. Flag conflicting duplicates and ambiguous taps; DO NOT silently remove a user's saved audio routes. Use accepted revision to recompute strip OUT, WIRE, Matrix, Navigator, scenes and undo from same projection. Persist intentional disconnected MAIN on Show restore.

Keep existing 10CH+4AUX/CH1 vendor VST3 and saved Show compatible until production ChannelId cutover is complete. Hide first-release +BUS/+FX Return *creation* via capability flags, do not delete their data/runtime implementation or break old sessions. Universal channel can become submix/reverb return through ordinary incoming wires and its own insert rack. Exact installed capability flags are truthful, not future-proof visual decoration.

## H. Tests and independent proof

**Graph unit/property/model tests:** fixed seeds + hundreds of deterministic randomized edit sequences, canonical edge invariants, topological sort, cycles after WIRE+legacy SEND, WIRE+OUT, WIRE+split, recall SAFE, copy/paste, duplicate S-o-F, MAIN multi-incoming, invalid/stale epoch, crossbank, repeated add/remove, Undo. Compare GUI projections after every accepted event; no collision by channel display name.

**GUI Chromium/Playwright:** real rendered jack endpoints and paired stub anatomy across 1024/1366/1680/1920 widths, 768/900/1080 heights, Comfort/Standard/Compact, bank changes, 100%-150% scaling/zoom, collapsed WIRE, focus/unfocus, 0/1/N feeds, socket click/drag/replug/back/cancel/Escape, pointer capture/reduced motion; check selected edge binding and no fader/meter geometry regression. No fake audio proof.

**Native PCM golden:** 44.1/48/96k when supported, 64/128/256/512 frames, 1/2/3+ hops vs direct impulse same relative sample index; fan-in and fan-out expected amplitude within a test tolerance; no double process on a channel; fader/post, mono/stereo, disconnect/reconnect, PDC warning; VST3 instance lease and Show state. Screen interaction/graph mutation must not hold audio callback.

**Performance/reliability:** 16/32/64 admitted channels and multiple wires; source+FX densities, callback p50/p95/p99/max and deadline, xrun/capture discontinuity/output starvation distinguished, memory allocations, Windows native and UI frame time; target headroom under ~50% of callback period for supported reference normal workloads, long soak 60min 0FX/fullFX, physical loopback RTT vs reported latency. Document backend/device hardware; do NOT advertise 'faster/lighter than Waves LV1' without matched workload and environment. #15 0FX physical crackle unresolved and stop-ship regardless of CPU callback timing.

**Release chain:** One actual C++ producer fixture -> #271 consumer -> #270 validator against existing v8 schema, same exact integrated Windows SHA and executable, actual PCM/ports and physical FAT. Passing docs/GUI screenshots means PLANNED, not IMPLEMENTED.

## I. Original engineering references
- https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm
- https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm
- https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_trackprops.htm
- https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html
- https://docs.juce.com/master/classjuce_1_1AudioProcessorGraph.html
- https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioProcessor.html
- https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst.html
- https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
