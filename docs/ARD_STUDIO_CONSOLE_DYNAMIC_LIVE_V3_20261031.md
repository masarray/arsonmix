# ArSonMix ARD v3.0 — Single ChannelId Graph and Studio-style Bus Workflow
Date: 31 October 2026 · Technical architecture for #192, #95, #174 and #15

## Non-negotiable architecture
ONE JUCE AudioDeviceManager / input callback, ONE native immutable prepared audio routing graph, ONE authoritative session ChannelId roster and FX lease map. Do not add another WebAudio/JS DSP, plugin host, standby render graph or second callback. ArSonMix must remain a REAL-TIME mixer, not an audio editor. Use existing DynamicChannelRoster.h, DynamicPreparedMixerBlock.h and LegacyLiveGraphMigration.h, rather than rewriting a naive mixer from scratch.

## Domain / persistent state
- Stable uint64 ChannelId identifies each actual audio lane independently of display ordering and physical I/O pins. Roles: INPUT, GROUP, FX RETURN, MAIN and MONITOR. A VCA/DCA is a separate controller, NOT a signal-summing Bus.
- Channel: role, name, mono/stereo PROCESSING FORMAT, separately verified SOURCE binding, signed gain/pan/mute/solo, VST3 catalog refs + independently created native instance leases, primary OutputDestination, and zero-or-more parallel SEND edges PRE/POST with send gain, pan and enabled state. Naming source format vs processing format vs physical pin count must remain independent.
- The V3 control-roster implementation tags primaryOutput on a ChannelSendRef to preserve current DynamicPreparedMixerBlock edge ABI; the single primary OUT edge is distinct from other parallel sends. OUT NONE removes only primary output. DSP graph compiler must prevent cycles, duplicate graph edges, invalid destinations, format mismatches and accidental dry duplication.

## Atomic Studio transaction
- Add Bus for Selected Channels accepts expected session revision + a set of unique source ChannelIds, creates a NEW independent GROUP ChannelId, replaces each selected channel's primary OUT with new Bus, adds Bus's primary OUT → MAIN, and leaves all parallel SENDs unchanged. One action/one validated candidate, no half-routed intermediate session and no stale index references.
- Add FX Channel for selected channels creates new FX RETURN, keeps source primary OUT as-is, adds parallel POST SEND(s) at selected send gain, and connects FX return to MAIN. Separate empty BUS/FX commands create new nodes without sources; source can be assigned later. Rename/reorder/delete/copy/duplicate use the stable ID.
- Stage off RT, validate resource budget and compiled topological DAG, prepare channel scratch and VST instances off callback, then only publish live after approved device-epoch / route-revision / MUTE and native ACK. Failures leave accepted audio byte-identical, no hidden unmute or auto hardware switch.

## DSP and GUI wiring plan
1. DYN0 stable ChannelId roster and DYN1A/B preallocated PCM graph already exist. DYN1C control-thread createBusForSelected / setPrimaryOutput plus two-mic PCM tests are now implemented in the M3 candidate PR, NOT live user actions.
2. Migrate current 10 CH + 4 AUX + MAIN + independent MONITOR/secondary output Show to ChannelIds, matching old exact PCM and preserving sparse ASIO callback ordinal translation, channel mode, fader smoothing, solo closure, mono MASTER after FX, vendor Orbit, native CH1 Show chunks, editor identity and VST3 sidechain.
3. Resolve legacy CH1 special-case FX ownership. Key per-ChannelId native lease through single immutable audio schedule. No second processing/render or per-callback heap/mutex/JSON, no copying plugin object while busy. Prewarm/prepare/destroy in quiescent control thread with bounded fade/retirement.
4. Expose one native ACKed SessionSnapshot containing IDs/order, accepted sources, primary destinations and sends, fan-in count, plugin instances, meter IDs, and epoch/revision. JS app/bridge/mixer-io/monitor are projections of that same snapshot. Draft state is separate overlay; no placeholder Groups or parallel JS 10CH registry.
5. Render same compact strip component regardless of role; only source-specific controls (no physical preamp on BUS) are semantically hidden. Highlight relations from accepted graph on selection changes, not animated per audio buffer. Auto-bank/virtualize N strips at 1366px, preserve pinned MASTER and all L2 geometry.

## Reliability / vetoes
- Real-time callback: bounded preallocated buffers, no allocations/locks/device enumerate/VST lifecycle/graph sort/JS IPC. The renderer's prepared graph owns mono/stereo input, external FX callbacks via stable id, PRE/POST sends and physical MAIN/MONITOR split. Topology compile remains O(V+E) control-side with memory and CPU admission; do NOT promise unlimited active channels.
- MUTE→APPLY→native revision ACK governs structural changes; continuity and crackle must be tested physically. Realtime fader/pan/mute/send gain changes use smoothed RT-safe parameters, not graph rebuild.
- Regresion gates: old 10CH/4AUX/CH1 Orbital native state and Show, all M1/M2B and DEVICE4 source/device switching, master/monitor/secondary output route, independent streams and factory FX provenance, low-end GUI.
- 0-VST crackle issue #15 and true roundtrip ASIO/WASAPI measured latency are NOT solved by green Windows CI. Refuse to mark hardware FAT complete without real audio.
