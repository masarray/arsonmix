# G5B — Native FX multi-insert transaction and rack, 30 October 2026

Engineering contract for [issue #95](https://github.com/masarray/arsonmix/issues/95). Base: G5A merged into main (7cbf04e). Read current R6 roadmap / PR #184 before further changes; this branch deliberately avoids modifying `NativeDryRouteCommand.h` and the channel-format routing code owned by MS1.

## Implemented in this branch

1. **One RT graph, eight inserts per routed lane.** `LiveDryRoutingLab::FxSelections` packs up to 8 ordered catalog selections for each of CH1–10 and AUX1–4. Missing index, 9th insert, unknown catalog ID, invalid stereo effect or nonzero reported plugin latency fails transaction before graph publication. Identical previous device-epoch/revision and catalog identity reuses real `FxInstance` objects (exact slot first, then first available for safe reorder). No extra callback or allocation in the audio thread. The original CH1 8-VST chain+Orbit is mutually exclusive with CH1 routed inserts.
2. **Versioned-like additive request shape.** Channel/AUX `fxIds: ["id1", "id2", ...]` is an ordered bounded array; `fxId` stays as legacy alias for slot0 and must agree. Catalog resolves every ID on the JUCE message thread. Only one `routingDryApply` action and one acceptedRoute/native ACK; failures leave the old prepared graph untouched.
3. **Operator interface.** For CH2–10, MIX shows first six real slots, while FX RACK shows all eight with ADD/REPLACE, UP/DOWN, REMOVE and a single APPLY. No fake Master inserts, no automatic output unmute. `web/monitor.js` owns the sole routing draft; mixer, plug-in picker, Routing and FX Rack project from the same draft and authoritative accepted snapshot. Exactly the same physical source mapping/fader/send plan is carried through a structural FX apply. No re-instantiation on GUI navigation. CH1 legacy workflow remains separate until unified native graph API is validated.
4. **Progressive compatibility.** Pre-G5 route documents with only `fxId` still decode and apply. The old named status `channelFxNames` retains slot0 name; the new UI uses the acceptedRoute `fxIds` to render the ordered chain. PRE taps remain after all channel inserts; POST after fader.

## Acceptance gates

- Exact-head Chromium `tests/mixer_io_smoke.py`: CH1 and MASTER stable, CH2 physical MONO patch ACK, two VST3 draft no implicit apply/unmute, MUTE-gated APPLY serializes two IDs, negative ACK does not change live chain, positive ACK has both, draft reorder then REVERT preserves live order and six fixed mixer rows at all viewports.
- Windows JUCE MSVC + CTest: existing `live_channel_fx_graph_test.cpp` proves two actual JUCE processors in CH2 sequentially and pointer-stable reuse after graph reorder, zero RT allocations and no premature release. Exact-head ZIP/package integrity required before users test.
- Real hardware FAT remains open: ASIO 2-in/2-out and multiport source; independently load first/second/reorder/remove FX and listen for crackle, isolate input 2, confirm no CH1/Master state change, retest ASIO/WASAPI reopen and vendor state.

## Explicit current limits

- Routed multi-insert BYPASS, individual vendor editor/Orbit, gain compensation/PDC, master post-sum FX, and full portable Show state restoration for every slot **not delivered**. Do not draw enabled bypass for these lanes or advertise PDC for latent plugins; `LiveDryRoutingLab` explicitly rejects nonzero latency until compensation exists.
- MS1 channel format (PR #184) changes `LiveDryRoutingLab.h` and monitor; reconcile by additive merge, not overwrite; no merge during unresolved conflict. Existing input source mono/stereo conversion is not rewritten by this feature.
- Never claim actual no-crackle hardware qualification from CI. Issue #15 stays open.
