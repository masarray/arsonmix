# ADR-R5 — Native Mono/Stereo Channel, Bus and Audio I/O Contract
Date: 2026-10-30 | Status: CANONICAL TARGET — not a claim of implementation
Read with canonical PRD v2.0 / ARD v2.0, GUI_WORKFLOW_BENCHMARK_20261030.md, issue #170, and current source. Source baseline main@890cc310: G1/G2 and G3 merged; physical hardware FAT #15 unresolved. References: https://www.waves.com/emotion-lv1-tutorial-3-3-channel-window-inputs and https://www.waves.com/emotion-lv1-tutorial-3-6-channel-window-aux-sends; official Studio One Audio I/O Setup mono/stereo logical patch workflow.

## ADR-R5-001 — one authoritative typed graph; source != processing channel != bus != output
A PHYSICAL_MONO/STEREO source and a MONO/STEREO processing channel are different concepts. Source identity, source intrinsic format, channel processing format, bus format, send conversion, and physical output destination must each be explicit fields. One mono physical pin can feed stereo processing with deliberate pan; a stereo pair can feed a mono channel with explicit downmix. A channel with NO physical input can still have MONO/STEREO format for an internal BUS receiver. A bus may be MONO or STEREO independently of its source. No UI-only changes or second audio engine.

Native reality: ChannelRoutingContract.h already has ChannelFormat(followSource,mono,stereo), effectiveFormats, foldStereoToMono and sparse physical-port callback mapping. NativeDryRouteCommand.h still treats a single mode (NONE/MONO/STEREO/BUS) as both source and processing format; BUS forces STEREO; precisely 10 channel rows required. PreparedMixerBlock.h has mono equal-power pan and stereo balance, but uses stereo scratch and routed VST3 uses prepareStereoEffectLayout. ChannelSendPlan.h/AuxBusPlan.h compute stereo balance coefficients; current sends are NOT format-aware mono pan. Four dedicated AUX FX buses are currently stereo, physical output only MAIN L/R readback. GUI mixer-io.js suggests adjacent stereo pairs but advanced patch can choose non-adjacent real L/R. Legacy web/app.js preview has index-based mono/stereo defaults. These are exact seams to evolve, not evidence the new contract already exists.

## ADR-R5-002 — conceptual, versioned state model
- DevicePin: stable device-scoped pin ID, capture/render direction, negotiated callback index, clock domain and enabled mask; never persist friendly name or callback ordinal as unique ID.
- SourceBinding: kind NONE / PHYSICAL / BUS / PLAYBACK(future); sourceFormat MONO/STEREO when applicable, explicit leftPinId and optional rightPinId, with distinct active stereo pins; physical source can fan out to multiple logical channels.
- MixerChannel: immutable channelId, display name/color, processingFormat MONO/STEREO, sourceBinding, panMode, inserts, send edges, fader/mute/solo. Optional legacy FOLLOW_SOURCE only during migration; not a substitute for user-selectable format.
- Bus: immutable busId and type FX_AUX / GROUP / MONITOR / MAIN / MATRIX / CUE; processingFormat MONO/STEREO, independent inserts, meter, fader, routing. DCA is CONTROL-ONLY, not summing audio. Existing 4 AUX are stereo until the engine implements native mono AUX.
- SendEdge: immutable source/destination IDs, PRE/POST tap, gain, enabled, pan/balance, explicit format conversion and latency path. Physical output patch is a separate downstream edge with port ownership constraints.
- Session: schemaVersion, logical channel/bus IDs, sourceFormat/channelFormat/busFormat, physical stable IDs, FX vendor state and accepted graph epoch/revision. Restore must migrate old mode fields losslessly or reject clearly.

Proposed interface wire fields (new version; not already implemented):
source: {kind:NONE|PHYSICAL|BUS, format:MONO|STEREO, leftPinId, rightPinId?, busId?}
channel: {channelId, name, format:MONO|STEREO, source, panMode, inserts, sends}
bus: {busId, kind, format:MONO|STEREO, inserts, mute, fader}
edge: {fromId, toId, tap:PRE|POST, gainLinear, pan, enabled, conversion}
transaction: {schemaVersion, deviceEpoch, graphRevision, expectedPluginRevision, draft}
Use compact validated native enum/IDs for the actual C++/JS boundary, not parallel JS routing rules.

## ADR-R5-003 — format conversion truth table and DSP policy
| Source | Destination | Required signal rule |
|---|---|---|
| mono physical | mono channel | one logical mono stream, equal-power mono PAN at stereo summing boundary |
| mono physical | stereo channel | explicit mono-to-stereo projection; pan feeds L/R, no fake second physical input |
| stereo physical L/R | stereo channel | preserve independent L/R samples, BALANCE not mono pan |
| stereo physical L/R | mono channel | documented 0.5*(L+R) once before FX (existing ChannelRoutingContract behavior), anti-phase cancellation disclosed |
| mono channel | stereo FX AUX/GROUP | controlled mono pan projection at send destination; prevent accidental +6dB |
| stereo channel | mono bus / physical output | explicit single downmix at destination boundary, never implicit twice; power/headroom tested |
| stereo channel | stereo bus | preserve L/R, no accidental cross-feed or width reduction |
| mono bus | stereo hardware pair | explicit bus-to-L/R map, only with native output patch implemented |

Flip MONO->STEREO is a structural operation that preserves channel identity, other FX/sends and current left input. Right input must be chosen explicitly unless source stays one mono pin with deliberate projection; never automatically invent a neighboring capture pin. Flip STEREO->MONO must show 0.5(L+R) policy and retain previous R mapping in reversible DRAFT/Show metadata; do not destructively drop physical pin identity or silently downmix unrelated sources. Source type and channel type are independently editable; bus format does not change just because a channel format changes. MONO PAN uses equal-power coefficients, STEREO default BALANCE without crossfeed. Stereo width, dual pan, custom downmix laws and new taps are future features, not advertised before DSP exists.

Plugin compatibility: actual negotiated mono/stereo VST3 input/output buses, sidechain, mono-in/stereo-out, bypass and latency must be prepared off callback. Current stereo-only adapter does NOT guarantee mono plugin support; for incompatible layout choose an audited explicit adapter or REJECT without mutating accepted graph. Maintain existing CH1 multi-insert, DearVR Orbit and vendor state. Without PDC, keep existing latency fences on parallel routes; do not pretend phase aligned.

## ADR-R5-004 — safe transaction / RT requirements
User changes channel-format badge M or L/R or bus format in a SELECTED CHANNEL / BUS inspector. GUI stages typed DRAFT; switching tabs never publishes sound. Native control validates physical enabled/sparse mask, unique stereo L/R, stable endpoint identity, device epoch, graph revision, format compatibility, all send edges, DAG cycle, output pin ownership, FX layouts, scratch sizes, latency plan and resource budgets. Build prepared immutable graph off RT, reuse existing graph publisher and plugin ownership. Only user MUTE + APPLY + positive native ACK makes LIVE; rejection leaves prior accepted graph intact, no automatic unmute or silent implicit mono/stereo fallback.

No allocation, mutex, filesystem, string/JSON parsing, VST factory/layout negotiation, new audio driver, UI events or graph destruction in callback. Preserve current bounded 10CH + 4 stereo AUX + 32 sends as default; format work does NOT silently increase channel/bus limits. Any future 8/16/32 capacities must be measured and user-configurable separately.

## ADR-R5-005 — mandatory native/UI/hardware acceptance
MS01 mono input->mono channel->stereo MAIN: correct pan and one source.
MS02 mono input->stereo-format channel with NO second physical pin: works only via explicit supported projection, no phantom capture.
MS03 different L/R tones->stereo channel: separation maintained, stereo meter correct.
MS04 in-phase identical stereo->mono: no +6dB; anti-phase channels cancel per declared 0.5(L+R).
MS05 stereo L/R non-adjacent sparse enabled ports: correct callbackIndex, not guessed label or ordinal.
MS06 same-pin stereo, disabled R, driver disconnect, stale epoch: REJECT, old accepted graph intact.
MS07 disconnected channel / BUS receiver can be independently MONO or STEREO; legacy BUS forced-stereo removed under v2 command, legacy migration verified.
MS08 two physical mono channels send PRE/POST to one stereo AUX reverb: one FX instance, correct channel-format-aware pan/gain and mute/solo.
MS09 stereo source into MONO group; MONO group into STEREO main: conversion exactly once, level/meter measured.
MS10 VST stereo-only/mono-only/mono-in-stereo-out/sidechain and latency change: valid negotiated layout or explicit REJECT; no lost vendor state.
MS11 migrate legacy Show format/source fields + physical rebind/undo, preserve relative FX state and route meaning; missing hardware rejects safely.
MS12 flip format repeatedly with CH1 2 native VST/Orbit while switching FX/ROUTE/SENDS 20x: plugin instance identity and GUI layout preserved.
MS13 two-output device refuses HW3/4; 8-output MAIN 1/2 and MON 3/4 with mono bus/output only after dedicated OutputPatchPlan.
MS14 native 48k/128 and 48k/480 with 0FX and representative FX, callback deadline, audible crackle and sample continuity. Issue #15 remains independently open until user hardware FAT.
MS15 UI at 1366/1440/1680, integrated GPU: format/bank visible, no fader geometry jumps, no 6 phantom patch columns for 2-input device.

Test by deterministic C++/CTest, native ACK/negative ACK, js smoke, exact-head Windows ZIP, **real hardware FAT**. Never call UI-only output or Chromium success an audio certification.

## ADR-R5-006 — implementation milestone boundaries and code owners
MS1 [meaningful vertical slice]: independent v2 source/channel format codec, legacy migration, correct control-thread compile and native effectiveFormat status; actual mono/stereo conversion/pan and channel format flip UI; regression and Windows ZIP. Touch ChannelRoutingContract.h, NativeDryRouteCommand.h, PreparedMixerBlock.h, LiveDryRoutingLab.h, mixer-io.js, monitor.js, focused tests; preserve CH1 FX. Existing #109 becomes baseline dependency.
MS2 [format-aware send/bus]: typed MONO/STEREO bus receivers and edge conversions, mono send pan / stereo balance, AUX return and Group policies, VST bus layout/PDC planning. No MON physical output yet. Reuse existing prepared graph; no parallel DSP graph.
MS3 [physical output]: extend existing issue #143, not a competing patcher. Native multi-out bus->render ports and M/L/R hardware matrix; 2-output reject and 8-output simultaneous MAIN/MON proof. GUI focused channel/bus inspector, logical I/O naming; no phantom outputs.
MS4 [quality]: all-channel FX multi-insert (#95), plug-in bus negotiation and bounded PDC, full portable Show state and recovery, #15 hardware qualification. Channel bank capacity expansion after performance evidence.

Work sequence core tests -> typed native command -> prepared graph -> actual native ACK -> UX -> exact-head CI ZIP -> hardware FAT. Parallel coding must partition owners; C++ routing shared file owned by one integration owner, GUI controller shared file by one owner; reconcile current main before every PR and avoid stale #115/#117/#128 bulk merges. No GPU-heavy filters or full mixer DOM replacement on format change.

## Explicit non-goals
Not DAW sequencer, unlimited tracks, copying Waves/PreSonus branded art, VST2 promises, unsupported stereo plugin negotiation, unverified physical monitor outputs, automatic neighboring pin assignment, unguarded silent format changes or a new audio callback.
