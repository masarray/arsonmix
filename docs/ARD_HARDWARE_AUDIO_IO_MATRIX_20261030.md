
# ArSonMiX — Audio Source/Patch Architecture Decision (PATCH2 ARD)

**Revised:** 2026-10-31 | **Baseline inspected:** main 498ded6678 | **Decision:** target architecture / technical handoff only | **NOT IMPLEMENTED:** live-safe one-click source change, logical reusable source registry, or multi-device capture.

**Supersedes previous matrix-first UX architecture, NOT the accepted native routing contracts.** See [PATCH2 operator PRD](PRD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), current [PATCH1 implemented control flow](PATCH1_OPERATOR_SOURCE_TO_CHANNEL_20261031.md), approved [L2 design lock](../AGENTS.md), [convergence gates](CONVERGENCE_RELEASE_GATE_20261031.md).

## 1. Verified reference and why PATCH1 is not enough

- [Waves LV1 Channel Inputs](https://www.waves.com/emotion-lv1-tutorial-3-3-channel-window-inputs) configures sources on a selected channel via device-grouped dropdown. [LV1 Patch Window](https://www.waves.com/emotion-lv1-tutorial-2-2-using-the-patch-window) patches real global channels from a meaningful typed matrix (Input/Output/Internal); [Mixer Route mode](https://www.waves.com/emotion-lv1-tutorial-4-10-mixer-route-and-channel) avoids navigating to a separate page for ordinary changes.
- [Fender Studio Pro Audio I/O](https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm) defines a durable device-independent software input layer, which the user selects from a track/console channel. [Fender Console](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm) selects inputs directly from top-of-strip controls.
- PATCH1 #218 has real native routing, but source selection is repeated across ten rows, requires each-row STAGE and global manual MUTE/APPLY. Its optional crosspoint and Advanced Bus/FX workspace are confusing when read as different patch managers. This is an operator workflow deficit, not proof native routing never existed.
- Current source owners: src/LiveAudioEngine.h (actual device), src/Main.cpp (JUCE setup/status/bridge), web/monitor.js (accepted/draft route state and apply), web/mixer-io.js (UI), web/mixer-io.css + standalone WebView2 HTML (presentation), NativeDryRouteCommand / LiveDryRoutingLab (schema/validation/ack). DYN1B-2A #215 adds OFFLINE PCM parity only. MS5 #219 adds AUX sends-on-faders; neither establishes the quick-switch path.

## 2. ONE source registry, ONE routing controller, ONE live audio graph

Desired flow:

JUCE device inventory (actual device IDs, enabled capture pin IDs, labels, format, epoch)
  → immutable PhysicalPortInventory snapshot
  → immutable NamedLogicalSources list (MonoPin | StereoPair | NativeBus, with availability)
  → ChannelSourceBinding {channelId, sourceId}, independent from processingFormat
  → web SOURCE picker on strip or Channel Focus
  → control-thread QuickPatch request {requestId, deviceEpoch, baseRevision, channelId, sourceId}
  → existing sole native route/graph planner, validation, preallocated immutable revision
  → audio-safe commit at approved block boundary
  → native positive ACK; all subscribed UIs display the same accepted binding.

**No JS audio DSP**; no new AudioContext, independent PCM callback, extra capture device opened by the UI, VST instance rebuild, per-pointer animation or parallel mutable routing graph. Keep all physical outputs and VST3 leases tied to the same runtime owner.

### Immutable schema (design, not an asserted existing C++ struct)

DevicePinKey: {deviceStableId, driverType, nativePinIndex, direction, epoch} — stable persistent identity PLUS epoch-specific resolution; pin index alone is not portable.
LogicalSource: {sourceId, userName, mode: MONO|STEREO|BUS, leftPinKey?, rightPinKey?, busId?, deviceScope, availability} — a logical alias, not a new audio stream. A pair must resolve to two distinct verified enabled capture pins of the same device graph.
ChannelSourceBinding: {channelId, sourceId, processingFormat, outputTarget, acceptedRevision}. Changing source does not silently change mono/stereo processing width; explicit compatible policy for mono→stereo and stereo→mono is required.
QuickPatchRequest: {requestId, deviceEpoch, baseRevision, sourceRevision, channelId, sourceId, expectedInstanceGeneration}. QuickPatchAck: {requestId, committedRevision, acceptedSourceId, acceptedPinKeys, result, failureReason}.

Typed candidate sources must never be inferred from a label like "Virtual" alone. MAONO PS22, PS22 Lite and PS22 Live may have different enumerations. The official PS22 Live Routing Center describes virtual and loopback capability, not guaranteed exposure of each channel as separate pins in every selected JUCE device.

### Relationship to current PATCH1

Existing __arMixRouteView, __arMixStageChannelInput, __arMixApplyMixerDraft, routingDryApply, arMixRoutingLabAck and DeviceEpoch/revision handling REMAIN authoritative for full/batch structural routing. PATCH2 must use or extend these same owners; choose a single internal commit primitive shared by full graph APPLY and source-only quick patch. A new request action may be added only with backward-compatible command schema, explicit result/revision and tests. Keep current manual-MUTE gate until new native source-only live-safe primitive is proven.

## 3. Separation of setup, runtime selection and advanced patch

**A. DEVICE SETUP**: select supported JUCE ASIO/WASAPI engine, verified device input/output pins, rate and buffer, real driver control panel. A driver reopen invalidates the old epoch, resets unfinished patches and forces explicit review of unknown/missing pins.

**B. SOFTWARE INPUT DEFINITIONS**: one-time creation and naming of reusable mono inputs, stereo pairs (with explicit L/R), and actual supported bus sources. Default mapping can be 1:1 where native pins exist; never create a phantom slot/virtual endpoint. Duplicate source bindings across mixer channels allowed when DSP contract permits. Saved per device and Show, with stable reconciliation on reopen.

**C. CHANNEL INPUT SELECTOR**: visible without switching to PATCH workspace, compact source field on selected strip, grouped provider list; one choose event. Mono CH3 from Input 1 and stereo CH6 from named virtual pair are two-intent interactions, after initial driver setup. Display PENDING until ACK; failure returns to old source. Search and keyboard navigation supported; no duplicate "STAGE" in everyday view.

**D. PATCH WORKSPACE**: power-user global matrix, explicit direction SOURCE → DESTINATION, typed tabs INPUT, OUTPUT, INTERNAL; filter by real device/source/bus, physical pin and selected channel. For INPUT only, sources appear along top and channel destinations on left (LV1-like). Stereo L/R row pairs distinguish sides. Matrix intersections dispatch to the SAME underlying draft controller, no new independent UI graph. Device-to-device is disabled/NOT SUPPORTED until its native backend exists; do not render clickable simulation. Large batches require review/apply/rollback. Advanced BUS/Fx placement is not mistaken for input patch.

**E. MONITOR/AUX/OUTPUT**: independent MAIN and MONITOR rendered physical buses, OUTPUT1 optional secondary playback, actual AUX1–4 internal FX returns; do not conflate a logical mixer output, headphone socket, VST send and a Windows virtual capture source. MS5 sends-on-faders still distinct from physical source assignment.

## 4. Live-safe source switch: RT safety state machine

**Present state:** stage → user physically disarms/mutes all required outputs → existing preflight → routingDryApply → native prepare → positive ACK. It protects already implemented controls; no change without a new native capability.

**PATCH2 goal:** quick swap of a SOURCE on an existing live channel of the SAME verified capture device, preserving FX chain, sends, fader, routing, channel processing, plugin state, mute/solo and Show state.

Proposed states:
IDLE → REQUESTED → PREPARED → SWITCHING → ACKED (or REJECTED / CANCELLED / REMAP_REQUIRED).

1. UI resolves selected logical source against the accepted immutable inventory and re-validates device ID, pin availability, source format, connection state, native revision and request generation. Optimistic UI ONLY says PENDING.
2. Control thread constructs a bounded immutable replacement source-binding/graph revision using existing plugin instance leases and prepared audio scratch. Do NOT re-open ASIO/WASAPI, instantiate VST3, add buffers, perform JSON parsing or block the audio callback.
3. Audio thread completes an owner-controlled atomic handoff at an existing block boundary. Keep the old graph alive until retirement acknowledgment and safety point; avoid use-after-free. Bound any interpolation/de-click on route replacement; **test** no unexpected click/dropout with live PCM. Do not assume a simple pointer swap is inaudible.
4. Positive ACK from native must match device epoch, request ID and committed revision. UI changes accepted text/source only after ACK. Negative/timeout/stale ACK retains former route, error message, and existing operator output mute state. A deliberately MUTED output is never auto-unmuted; failed source switch may not activate MAIN/secondary endpoints.
5. Safety exceptions: source format rebuild, device reopen, pin format mismatch, bus topology change or FX/Show revision conflict returns NEEDS_SAFE_APPLY. Display one concise explicit prompt (not multiple STAGE/MUTE/APPLY widgets); submit only via existing muted structural transaction. If live-safe quick patch cannot be proven, keep old user safety flow and label the future one-click UX NOT IMPLEMENTED.

**No false "zero latency":** buffer/device/FX round-trip latency remains physical and measurable. Do not add latency or audio callback allocations; measure signal discontinuity, CPU callback peak, throughput and xrun under issue #15 FAT.

## 5. Portability and MAONO virtual channel fidelity

- Device-specific default source profiles keyed by stable device identity, driver type and native pin identities; session/Show stores logical source IDs and binding preferences. On new device, match only unambiguous verified pins; missing ones get REMAP REQUIRED, never silent IN1 fallback.
- Input names such as "MAONO VC In L/R" are user/device labels, not a promise those capture pins exist. PS22 Live's Routing Center may expose analogue/OTG/virtual/loopback differently from PS22 standard/Lite and differently by driver. Validate first with native JUCE inventory. Windows virtual playback endpoint from an application is not necessarily usable as a capture port on active ASIO.
- Additional simultaneous capture from a separate device/Windows endpoint requires native independent clock sync/ASRC, hotplug recovery, bounded buffering and field tests; explicitly **OUT OF PATCH2** unless that engine milestone is designed, implemented, and verified.

## 6. Performance / ownership / packaging

- Render source popover on actual user click and on source inventory/ACK change only; prefer diff updating the focused row. Coalesce native status telemetry, no 12Hz full matrix rebuild or pointer-driven DOM repaint; use virtualized rows only after real DYN2 channels exist.
- Retain original #176 L2 11-strip layout, pinned MASTER, RAIL3 cyan fader, G5 native FX/editor, #219 sends-on-faders, existing OUTPUT1 secondary path, independent MONITOR. Do not steal fader area to place a giant I/O block.
- Source CSS lives in web/mixer-io.css; standalone file and JUCE WebView2 compiled assets must stay in CI parity; make no browser-specific audio engine assumptions.
- Source/pin registry is immutable and shared from native status, not duplicated ad hoc maps in monitor.js, mixer-io.js and bridge.js; no competing singletons. Do not create a new worker merely for 10 source labels.
- Use small tests (Node contract, Chromium two-click and matrix smoke) for GUI; native Windows CTest for route planner/atomic ACK; physical PS22/iGPU FAT separate. Change code on one coherent PR; minimize redundant GitHub CI.

## 7. Acceptance matrix: BLOCK release if violated

- **Q1** Source dropdown CH3 → Input 1 with ≤2 user interactions, device already configured; pending→native accepted status with no extra manual STAGE. Exact audio continuity FAT while armed.
- **Q2** CH6 → named MAONO virtual stereo pair with one source selection; measured L/R channel identity, physical nonadjacent pins supported, distinct input pair validation.
- **Q3** Driver loses/reports different pin; negative ACK/revision rejects, old route retained, UI shows REMAP REQUIRED and no phantom inputs.
- **Q4** Legacy full patch and ADVANCED crosspoints share same native revision; no duplicate port/data owner, mock+hardware tests, correct stereo L/R direction and keyboard operation.
- **Q5** Dynamic channel/MAIN+MONITOR/secondary output, 4 AUX sends, FX state and Show unaffected; no extra PCM graphs/plugin recreation.
- **Q6** Window resizing at 1024/1366/1440/1680 preserves L2; browser/Windows on standard iGPU no observable pointer stutter caused by source popover.
- **Q7** ASIO/WASAPI 44.1/48k, buffers 64/128/256 as supported; 0FX field crackle issue #15 remains explicit P0 blocker. CI PASS != real physical FAT PASS.

**Implementation gating order:** (i) source registry/profile and direct picker that still transparently uses current safe staged transaction (no misleading quick-live), (ii) real native safe single-input commit + ACK, (iii) remove manual global APPLY from routine same-device changes ONLY after (ii) passes FAT, (iv) unify advanced matrix and named sources/Show remap. Avoid one more UI-only layer that simulates native functionality.

