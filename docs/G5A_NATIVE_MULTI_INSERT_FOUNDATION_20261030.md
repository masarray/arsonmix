# G5A — All-channel insert foundation and honest plugin UX (2026-10-30)

**Scope:** G5A is a *real native graph-capability milestone*, not yet the complete all-channel VST3 user workflow. Parent requirements [issue #95](https://github.com/masarray/arsonmix/issues/95), [ADR R5](ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md), [ARD v2](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md).

## What this PR actually delivers

- `LiveChannelFxGraph` now has a bounded **8 sequential VST3 processors for each of 10 logical input channels and four internal FX AUX returns** (14×8 rack capacity). Existing `install(lane,...)` and `reuse(lane,...)` retain original single-insert semantics; new control-thread-only `installAt(lane,slot,...)`, `reuseAt(lane,newSlot,prior,oldSlot,id)` and readback interfaces allow future atomic rack transactions.
- Processor instance sharing/reordering never happens in callback. A new graph may reuse an *identical* vendor instance at a different order position on the same lane; duplicate reuse is blocked. Packed/no-hole insertion, fixed capacity, and single RT callback are enforced.
- The PRE bus tap continues after the entire sequential channel insert chain, with POST after the source fader. A real JUCE gain-effect test executes **two actual processors on CH2 in sequence**; checks output ×6, same processor identity after reordered graph retirement, no callback heap allocation, no premature release, capacity/gap/duplicate rejection.
- Picker: star-only accessible Favorites toggle and six equal categories with **COMP** as a visual alias for native `Dynamics`, without changing plugin classification. Manager: two organized horizontal groups with short headings, consistent 114×34 mm-style BTN-180 controls, readable hierarchy and overflow-safe narrow layout.
- Native loaded rack captions use slightly larger green Inter, aligned with ADD FX. Empty placeholders are not green. Master preview no longer displays six clickable **fake** `ADD FX` actions; it explicitly states Master native FX is unavailable.

## What is intentionally not represented as delivered

- **Native command/API:** `src/LiveDryRoutingLab.h` and current accepted route schema still select **one** VST3 per routed lane/AUX (`fxId`). The eight-slot core is now implemented, but CH2–CH10 extra INSERT buttons remain **disabled**, not fake success. To make them live requires extending `NativeDryRouteCommand` payloads, validated eight-slot catalog IDs+stable instance identifiers per channel, graph revision + epoch + MUTE-gated APPLY, native slot ACK, frontend `bridge.js` and `plugin-ui.js` per-channel actions, and full Show state. Existing CH1 multi-insert/Orbit must be reconciled under one authoritative controller, not an additional audio callback.
- **PDC:** Plugin latency-aware parallel PRE/POST send alignment and bus compensation are not built. Keep the current native zero-latency plugin guard in place, reject unsupported latency, then add bounded preallocated path compensation before advertising all-latency compatibility. This commit does not change that guard.
- **MASTER FX:** Not native; no cosmetic interactive slots. No master post-sum DSP extension in this change.
- **Hardware:** CI verifies code and simulations, not physical ASIO/WASAPI dropout/crackle. Issue #15 stays open.

## Release gates

One exact-head Chromium geometry/smoke plus Windows MSVC/CTest before merge; include `live_channel_fx_graph_test` under native tests. After this, next staged G5B PR should implement end-to-end **CH2 add first & second VST3 → graph ACK → process 2 blocks → bypass/move/remove → save/reopen**, negative ACK rollback, no audio allocations, and no changes to CH1/other channels during graph swap. Do not merge pre-G5 branch back into latest main without reconciling concurrent MS1/MS2/G4.

## Studio One mental model

Studio One groups ordered **inserts** per console channel independently from PRE/POST **sends** to FX returns. In ArSonMix, these remain one native channel processing path and a separate native routing graph; FX UI switching never re-initializes vendor processors. The design follows observable workflow, not third-party assets.
