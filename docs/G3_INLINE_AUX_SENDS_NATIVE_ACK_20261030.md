# G3 — Studio One-style channel SENDS operator milestone (2026-10-30)

**Implementation branch:** `feat/g3-inline-aux-sends-ack-20261030` stacks on G1/G2 PR #167. No overlap of audio callback or duplicate graph. This doc records actual implemented scope; it is not a claim of hardware FAT.

## What users can actually do
1. In MIX select **SENDS**. The FX rack stays mounted and its six slot rows/fader/meter/strip heading retain geometry. All four **internal stereo FX AUX** destinations are visible on each channel, even if not connected.
2. Click the AUX row for CH1–CH10. A compact non-blurred focused editor opens **without shifting the mixer**. Enable AUX return, optionally set RETURN level, MAIN assignment, or MUTE. Add independent PRE and/or POST taps; adjust send level and pan, bypass temporarily (real zero-gain edge), restore previous level, or remove a tap. Existing AUX native FX remains configured in the separate Routing editor.
3. These are **DRAFT** operations. Never label them as LIVE on click. Use output **MUTE** then **APPLY** in the send inspector or bank; the original `routingDryApply` validates port identity, device epoch/revision, enabled channel/AUX, bounded <=32 sends, duplicates and feedback DAG, then publishes only after native ACK. Rejected requests do not change the live native graph. REVERT restores the last accepted route.
4. PRE tap = post-insert, pre-fader; POST tap = post-fader. These are real `PreparedRenderGraph` / `PreparedMixerBlock` paths, not UI-only; `tests/dedicated_aux_bus_test.cpp` covers zero fader/PRE, mute AUX, duplicate and capacity behavior.
5. A SENDS row does **not** mean physical AUX hardware output. Only currently supported MAIN L/R are wired to real output jacks. A disconnected physical input may not publish a send.

## Safety/engineering notes
- Real source of truth: `src/LiveDryRoutingLab.h` and its native ACK. `web/monitor.js` owns the **only** JS drafts, including `draftSends` and `draftAux`. `web/mixer-io.js` is a passive view with tiny typed event commands, no second graph, no local audio process, no plugin re-instantiation.
- Gain 0 means actual silent native edge; bypass preserves last nonzero gain per active draft/device. It consumes one send slot; REMOVE frees the slot. No unsupported "enabled" bit is sent to native.
- Device disconnection invalidates draft epoch. Live status rerenders only on topology/ACK change; no pointermove, per-meter or GPU blur work.
- Existing channel FX, FX Rack/Orbit, bypass/reorder and independent ROUTING workspace remain authoritative and unchanged.
- **Still not delivered:** full 8-insert FX per CH2–10, arbitrary hardware AUX output ports, hardware 0-VST crackle conformance (#15), or complete stateful Show/preset binary replay across device remap.

## Gates before main/release
G1/G2 PR #167 must land first. Run one exact-head Chromium `tests/mixer_io_smoke.py` including new AUX draft/ACK/rejection/revert and 1366/1440/1680 shots, one Windows MSVC / CTest + package verification. Real ASIO/WASAPI test: Mic1→CH1 EQ→POST AUX1 reverb→MAIN; POST follows CH1 fader, PRE remains audible at zero source fader, AUX MUTE silences return, no click/crackle, plugin survives navigate/settings changes. No hardware-qualified claims from CI alone.
