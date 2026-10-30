# EXECUTION HANDOFF — Mono/Stereo Native Channel / Bus Reform
Date 2026-10-30 | ADR: ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md
Read README, AGENTS, canonical PRD/ARD, UI benchmark and current code HEAD first. This file extends, not replaces, EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md.

## Exact starting point (historical, verify latest)
main@890cc310: #166 real CH1 VST ADD, #167 FX/ROUTE/SENDS and Audio I/O, G3 native ACK PRE/POST send inspector. Native 10CH/4 stereo AUX/32 edges, one active stereo hardware MASTER L/R, other output patch not implemented. ChannelRoutingContract.h can represent independent channel format and stereo fold offline. NativeDryRouteCommand.h currently couples GUI mode to source/channel; BUS always stereo. ChannelSendPlan/AuxBusPlan use stereo balance, not format-aware mono pan; FX prepareStereoEffectLayout. Legacy web/app.js indexes strip format for visual preview. #15 zero-FX hardware crackle remains open. Change HEAD and PR audit before all work.

## MS1 — must deliver a whole native+UI format flip, not a decorative selector
1. Add versioned typed sourceKind/sourceFormat/channelFormat to NativeDryRouteCommand and status, with legacy codec behavior tested; DON'T put logic in a second JS graph. A MONO or STEREO channel may have source NONE; a BUS receiver can be mono or stereo.
2. Validate physical L and R independently against sparse enabled pins, non-adjacent allowed, stereo same-pin rejected; retain right pin for reversible flip; avoid silently choosing neighboring pin.
3. Prepare exact conversions and pan rules on control thread, process in existing PreparedMixerBlock; preserve channel 1 legacy plugin chain and direct-route fence.
4. Selected-channel M / L·R badge in MIX/CHANNEL, simple Source Mono/Stereo and format selection distinct; share one native draft/ACK, one APPLY while MUTED, inspector for unsupported plugin/layout; stable fader/meter geometry and states.
5. Deterministic native tests MS01..07 and MS10..12, Chromium 1366/1440/1680, actual native ZIP after exact-head green. No "hardware live ready" absent #15 FAT.

MS1 acceptance: change format from mono to stereo, keep one physical MIC pin and get a real stereo-processed channel with defined mono pan/projection and no phantom R; change to mono from a true L/R source and hear/document 0.5(L+R), undo/negative ACK works. Repeated route and UI mode switching does not recreate VST or move faders.

## MS2 — format-aware sends and typed bus graph
Require mono->stereo AUX, stereo->mono GROUP, PRE/POST send policy, source format-aware pan, one shared VST instance, bounded PDC/layout compatibility; native DSP+UI committed together, no fake MON bus hardware outputs. Reuse #109 graph and current G3 editor.

## MS3 — real physical output patch
Extend #143 with native OutputPatchPlan for mono/stereo downstream render pins, stable pin ID and owned pairs, 2-out fail and 8-out independent MAIN/MON test, focused GUI matrix/selected route; only mark LIVE after actual ACK and hardware readback/FAT.

## MS4 — uniform FX and Show
After physical and PDC contracts, unify VST multi-insert across input/bus/master (#95), vendor mono/stereo layout negotiation, portability, real scene recall, rebind and rollback. No premature MIDI/DAW feature creep.

## Parallel orchestration
- Native command/format owner: ChannelRoutingContract, NativeDryRouteCommand, LiveDryRoutingLab, physical input registry, state migration.
- DSP/render owner: PreparedMixerBlock, ChannelSendPlan, AuxBusPlan, LiveChannelFxGraph, LiveAudioEngine; preserve callback, no allocations/locks/JSON.
- GUI owner: mixer-io, monitor, bridge, app, styles, all current fonts/fader materials; no duplicating routing model or replacing FX rack.
- Integrator/test owner: deterministic CTest, JS contracts, exact-head PR/CI, screenshot geometry and hardware FAT handoff.
Only one PR changes shared native DSP files at a time; owner aligns GUI and native changes in same vertical slice. If output patch and source format PR overlap, pause merge and rebase on latest exact SHA. No docs-only Windows CI or stale branch bulk merge. Always report WORKING / DRAFT / NOT IMPLEMENTED / HARDWARE UNQUALIFIED as separate categories.
