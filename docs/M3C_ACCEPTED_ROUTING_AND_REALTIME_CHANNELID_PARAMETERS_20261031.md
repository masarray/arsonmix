# ArSonMiX M3C — accepted dynamic Console routing + native realtime controls

**Product:** LIVE PERFORMANCE VST3 host mixer with Studio One Console-class subgroup workflow; no timeline/arranger/recorder.  
**Branch:** `feat/m3c-channelid-live-parameter-plane-20261031` (31 October 2026).  
**Important status:** engine-level preparation and deterministic PCM tests, **NOT the production JUCE callback cutover, no native +BUS GUI, and not a physical hardware FAT pass**.

## Delivered in this milestone
1. `DynamicPreparedMixerBlock` now separates immutable, control-thread-compiled ChannelId DAG/physical input/FX topology from atomic **per-ChannelId fader and pan targets** and **per-existing-parallel-send gain targets**. The one audio callback consumes preallocated state. No graph recompile, allocation, mutex, VST instantiate, or second DSP execution for fader/pan/established send gain.
2. Mono equal-power / stereo balance coefficients are published for each ChannelId; changes smoothly interpolate over at most 64 samples in the existing `process()` callback. Existing master, monitor, nested Group and FX routing remains one graph. PRE and POST sends use the same bounded ramp; changing send tap, destination, bus membership, pan or exclusive primary OUT still requires structural compile + ACK.
3. `DynamicNativeSessionPublisher` exposes `setChannelMix(epoch,revision,id,...)` and `setParallelSendGain(epoch,revision,source,destination,tap,...)`. Guarded by the existing control-thread `updateActiveControl` expected revision and device epoch. Invalid channel, NaN, wrong epoch/revision and missing/primary edges are rejected without modifying the accepted topology.
4. `DynamicConsoleRoutingView` is compiled from the **same roster as the successfully prepared native PCM snapshot** and published within that snapshot. It contains stable ChannelIds, roles/names/formats, exclusive primary OUT (zero means NONE), parallel PRE/POST SENDs and Bus/FX fan-in source lists. A source is counted once even if both primary and parallel routes lead to the same destination. Native accepted revision/epoch gates access; browser must NEVER substitute its own draft as LIVE routing.
5. Extended the single native mixer routing CTest with actual MIC→GROUP→MAIN + MIC→FX→MAIN PCM. Covers fader/PAN/send ramps without graph revision increment, main vs parallel paths, stale native guards, hard MUTE gate and revision-scoped source counts; keeps existing 16-lane M3B, legacy migration, 10CH golden PCM, MAIN/MONITOR tests.

## Safety boundaries
- Existing C++ graph is **not switched into** `src/LiveAudioEngine.h` or `src/LiveDryRoutingLab.h` in this PR. The real application continues using its incumbent proven 10CH+4 AUX audio callback and VST3/Show owners.
- Dynamic graph mute/solo remain compiler-prepared selection semantics; a runtime toggle cannot be called safe just because fader/send gain is realtime. Changing MUTE/SOLO, adding/removing a channel or attaching a VST requires the next prepared-graph transaction with explicit native safety.
- This work does not report measured latency, a real audio output signal, certified VST3 ownership on a new Group Bus, or Studio One GUI parity. Physical issue #15 (0FX crackles) still gates live acceptance.
- No UI button should advertise +BUS/CH11 until native callback migration is qualified and advertises a truthful capability/ACK.

## Next non-negotiable milestone: M3D *actual* native callback cutover
- Preserve CH1 vendor VST3 editor/Orbit and CH2–N FX bank (independent leases), vendor state chunks and existing Show.
- Compare old 10CH+4 AUX path sample-by-sample at equal controls and existing MAIN/MONITOR/secondary outputs before switching render.
- Install **one** dynamic prepared graph in **the existing JUCE callback**, not alongside the old graph, with safe plugin retirement + device epochs and output gates.
- Only after successful native Windows CI, PCM A/B and available physical 0FX/FX FAT expose +CHANNEL / Add Bus for Selected / FX Channel using `DynamicConsoleRoutingView` as the accepted Console state.
- Keep the Studio Console visual style compact, fixed LED thresholds, no CSS blur/expensive pointer hover and no full DOM rebuild per meter or fader frame.

**Reference:** official Fender Studio Pro [Effects Signal Routing](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm) and [Console IN/OUT and Bus fan-in](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm), PRD/ARD/Handoff v4.
