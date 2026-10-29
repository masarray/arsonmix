# R0B B2 — live native CH1 VST3 ADD/REMOVE without device callback detach

**Baseline:** main `9ad3f4c2c4df207e8a1c5c5aa4c9b06680c9bc9f` (29 Oct 2026). **Issue:** #149. **Status:** submitted for exact-head Windows MSVC/CTest; NOT yet hardware qualified.

## User-visible, meaningful outcome

A singer/engineer can **ADD or REMOVE** a real native VST3 insert from CH1 while the verified DIRECT output path and existing 10CH+4AUX routing graph remain active, without a routine `removeAudioCallback/addAudioCallback` pair. CH1 FX MOVE/de-click (#153), dearVR Orbit, independent FX instances on other channels, AUX sends, physical port/epoch ACK and original compact GUI remain unchanged. This makes live insert editing more practical, but **does not** establish click-free ASIO/WASAPI FAT.

## Ownership and immutable-graph architecture

1. **Prepare (control thread):** validate active DIRECT device, graph idle, order idle, catalog UID, bus layout; instantiate + prepare VST3 against exact sample rate/quantum; reject any latency-bearing new insert while 10CH multibus is active (PDC not delivered); reject any stale device generation/rate/buffer.
2. **Compile (control thread):** `FxStructuralEditPlan` maps current logical rack order to stable physical IDs and selects the removed logical slot or appends a new one. Normalize candidate to canonical order and reuse shared owning references for **all unchanged vendor instances**, including their state and tails. Removed VST3 stays retained by the old graph. Only the new insert gets initialized; no repeated `prepareToPlay` for unchanged plugins.
3. **Publish (RT, existing callback):** `LiveGraphHandoff` stages a single prepared pointer, computes old block with a fade 1→0, switches at buffer boundary and computes next buffer at 0→1. Route-live path now acknowledges `handoff_.endCycle` too. No refcounting, allocations, locks, plugin life-cycle, vendor get/set state, backend restart, JSON or WebView on callback. Single RT producer assumption and direct-fence protection remain.
4. **Retire (message thread):** after RT ACK, old graph retires via existing `finalizeGraphSwitch()`. Custom `PluginRelease` destroys/releaseResources the plugin only when its **last control-owned snapshot** disappears; reused instance cannot be released accidentally during old graph teardown.
5. **User interface ACK:** native `arMixAddResult(ok=true)`, graph revision and true `emitChain()` occur only after callback graph switch and control-thread retirement. Orbits are paused during structural work, then rebound by pointer identity and resumed after ACK, preventing writes to deleted/reordered vendor nodes. Errors report immediately without fake success.

## Exact safety constraints

- INDEPENDENT dual-clock mode: **ADD/REMOVE blocked** (until backend routing/ownership is explicitly requalified). DIRECT backend + exact device fence required; no unsupported IO is silently remapped.
- Existing CH1 and AUX/multichannel external FX cannot be duplicated. One source render into main as before.
- Pre-existing graph pointers remain readable by callback only until it acknowledges switching, then control owns retirement. Never tear down old plugin in callback.
- FX structural requests are serialized: one pending graph plus FX MOVE serialization. Structural requests are rejected if the device is changing.
- Plugin PDC is not implemented. A new latent VST3 in an active multi-bus graph is rejected with a useful message.
- No claims of hotplug/no-ACK cancellation or device-restart sample-rate migration until B3 device state machine tests. Do not silently auto-unmute.

## Tests and acceptance

- Pure native deterministic `FxStructuralEditPlan` tests added to existing `ARMixFxOrderTests`: all rack lengths 0–8, all MOVE permutations, every REMOVE logical index, APPEND, full-rack overflow, invalid structural plans and shared reference lifetime.
- Native source contracts check ADD/REMOVE contain **no detach/attach**, staged processor preparation, unchanged shared ownership, de-click and callback ACK in the route-active callback, vendor retirement on control, and browser success only after ACK.
- Exact-head Windows native **MSVC + CTest** and package verification is required before merging. Do not trigger extra CI matrices or relax old tests.
- Field FAT #15 remains mandatory: 0-FX dry-through, CH1 insert/remove/reorder under actual 10CH+4AUX and Orbit, 48k/44.1k ASIO/WASAPI device matrix; log callbacks, render provenance, PCM continuity and actual audible clicks. Green CI ≠ hardware qualified.

## Still unresolved after B2

- **B3:** verified, silent/unpatched default 10CH production session and retirement of operator Legacy/LAB dual mode with device hotplug rollback/epoch-safe pending cancellation.
- True all-channel multi-insert rack, PDC, arbitrary physical OUT PATCH, full native VST Show recall/backup, offline plugin isolation, and real-device release FAT.
