# ArSonMix P1C — Acyclic per-channel sends and internal bus routing core

**Date:** 2026-10-28  
**Base:** P1B stacked PR #110 (which depends on P1A PR #108)  
**Status:** Prepared DSP core + deterministic native C++ tests. **NOT wired to live JUCE audio device or VST3.**  
**Related:** #109 track routing, #95 channel FX, #15 hardware 0-FX crackles, #85 portable workspaces.

## Native audio topology contract

One physical input may fan out to multiple strips by explicit selection. Each strip independently processes its input FX, post-FX/pre-fader signal, its gain/mute/pan stage, and sends to another strip. P1C compiles **up to 32 sends** between ten strips (bounded, deterministic). No arbitrary 32-channel hardware restriction is implied: 32 is the temporary explicit capacity for *send edges*, not devices or input channels.

`engine/core/ChannelSendPlan.h` provides:

- One `SendDefinition`: source strip, destination strip, pre/post-fader tap, linear send gain, stereo balance and enabled flag.
- `SendRequest::toMaster[channel]`: independent default main-out control (true by default). A sends-only channel can turn off its direct MASTER path, avoiding unwanted double-feed.
- **Cycle detector:** reject CH1→CH2→CH1 and longer loops and self-feedback before activation. A deterministic Kahn topological sort prepares the actual processing order. Enabling a rejected edge leaves the previous plan unchanged.
- **Signal correctness:** pre-fader send comes from the *post-FX pre-fader* source buffer; post-fader send comes from *post-fader/post-mute* source buffer, provided by the future host integration. Different tap choices are not silently interchanged. Pre-fader sends intentionally do not follow fader attenuation; final policy for how solo/mute affects pre sends must be explicit in the native mixer host.
- **No implicit feedback or implicit master sends:** MASTER remains downstream of all strips. A MASTER-to-channel return requires a separate latency/feedback-safe graph contract and is deliberately unavailable at P1C.
- Direct send routing is additive to the channel's main output; `toMaster=false` is the way to construct an explicit sends-only source. The actual output stage must respect this flag before the feature is enabled.
- Sends support per-edge stereo balance; they do not currently perform an automatic stereo-to-mono conversion. The independent format contract of P1B must select channel DSP width, and insert-plugin layout negotiation remains a separate requirement.

## Realtime performance model

All validation, float finite checks, duplicate checks, cycle sorting, output offsets and send gain/pan coefficients are computed **off realtime** by `ChannelSendPlan::compile`. Plan publication is **not** implemented yet. `dispatch` loops only over the active source's outgoing edges and the host-provided fixed sample count, accumulating into preallocated, already-cleared destination buffers. No heap, locks, sample-loop trigonometry, topology scans, telemetry string allocations, graph destruction or plugin creation in this primitive.

The future RT integration must provide correctly scoped scratch buffers:
1. At the start of **each audio block**, clear **every** per-strip destination/incoming-send accumulator exactly once; also prepare each strip's physical input scratch. Use `ChannelRoutingPlan::extractSource` **before** any internal-send dispatch, because raw extraction overwrites scratch. This input must not be gated by fader zero or MASTER mute; use `extract` only for the legacy post-fader/solo-controlled path. A pre-fader send must still receive its source at fader -∞, with explicit native mute/solo policy applied later. Never clear the destination after an upstream send has already written to it.
2. Walk `topologicalOrder` once, combining that strip's prepared physical input and previously accumulated upstream sends into its *own* preallocated input buffer, then run its VST3 chain exactly once. Capture its post-FX/pre-fader tap.
3. Apply fader/mute/solo/pan to a distinct scratch output, capture its post-fader tap, and dispatch each configured pre/post tap only from the correct buffer. Do not reuse a destination accumulator as a source.
4. `receivesInternalAudio(strip)` is true for any bus-return recipient, **even if its hardware SourceMode is disconnected**. The future host must not skip a return merely because P1A/P1B's physical-only `Strip::active` is false. The return's effective gain/mute/solo state must be applied separately, without processing an invented physical input.
5. Mix only `toMaster=true` strips to MASTER; run MASTER FX and final output controls once, then send to the selected driver output. `toMaster=false` prevents duplicate dry+send output.
6. The routing plan, channel VST instances, plugin editors, Orbit references and negotiated device source identity must be changed through a single safe graph epoch/handoff/ACK protocol. Never process the same VST3 instance concurrently in old/new graphs.

Input device masks, hotplug and callback port indices are owned by P1B + forthcoming device registry. P1C cannot prove stable hardware timing and must not be enabled on top of a failing zero-VST hardware FAT (#15).

## Tests / boundaries

`tests/channel_send_plan_test.cpp` verifies: zero-send defaults, deterministic 10-strip order, pre vs post taps, send pan and gain, fan-out, fan-in, main-out disable, cycle/self-loop rejection, duplicate edge prevention, invalid indices, gain, pan and tap, disabled edges, over-capacity, transaction rollback and invalid buffer guard, plus audio-return activity with no physical port. Windows Release `assert` is kept enabled even under `NDEBUG` so CTest is meaningful. The integration regression also compiles a real one-port `ChannelRoutingPlan` with source fader zero, proves `extractSource` remains nonzero while master accumulation is silent, and routes a live pre-fader send (plus stereo-to-mono fold and muted-source source read). No redundant GitHub Action matrix.

The planned GUI should offer a compact SOURCE chooser and channel Sends inspector. Only show a real send selection when both source and destination are native connected buses; all routed requests use native ACK/reject. No full DOM redraw on pointer movements and no fake meter.

## Delivery sequencing

1. P1A #108 exact-head green, then merge it after verifying current `main` and unrelated PR ownership.
2. P1B #110 exact-head green, retarget after parent merge; merge with no conflict and no regression.
3. P1C this stacked PR exact-head green; retarget after P1B merge.
4. Real device registry (stable endpoint/port IDs), immutable graph/device epoch lifetime, and true per-channel plugin bus negotiation.
5. Only after issue #15 clean 0-FX physical FAT: staged live 2-channel, 4-channel, then 10-channel hardware acceptance with sends, recall, Orbit, all FX operations, and measured xrun/callback deadlines.

**No claim of full live 10-channel FX, click-free sends or hardware performance certification is made by this milestone.**
