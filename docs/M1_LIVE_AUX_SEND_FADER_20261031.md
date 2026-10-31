# M1 — Real-time native AUX Sends-on-Faders
Date: 2026-10-31 · Scope: operator-level POST gain only; source = [Product Truth](PRODUCT_TRUTH_CROSS_THREAD_20261031.md).

## What changed: a real audible control path
The same existing native 10-input + 4-AUX mixer now allows the **gain of an existing POST CH→AUX send** to change WHILE audio is running. In MIX / SENDS choose AUX 1..4, enter SEND FADERS, use the visible channel send sliders/keyboard. One release/keypress issues a dedicated `routingAuxSendGain` with the **accepted** native epoch/revision and edge identity; it never calls `routingDryApply`. Native validates source 0..9, AUX 0..3, existing POST edge, finite 0..1 gain, exact accepted route, normal DIRECT device and absence of Show/backup. A positive `arMixAuxSendGainAck` replaces accepted UI readback, and rejection leaves prior gain; fader movement itself is not presented as LIVE.

## DSP boundary
The existing `PreparedRenderGraph::aux` is immutable. The prepared `PreparedMixerBlock` owns 32 bounded atomic gain targets and callback-exclusive current values, initialized from control-thread compiled plans. For existing POST send edge, the callback reads the target once, interpolates in the first <=64 samples of the next block and multiplies with the precomputed format-aware send PAN coefficients. A source that has no signal or is muted remains silent. There is **no second audio graph, VST call, heap allocation, lock, native device reset or per-event DSP topology compile**. An initial send of gain=0 can be raised because normalized PAN coefficients do not depend on compiled gain.

## Safety and exactly what remains DRAFT
**CREATE/REMOVE, PRE send edit, send PAN, AUX RETURN enable/disable, FX insert, source or physical output change are STRUCTURAL**. They still require MUTE/DISARM→APPLY→native revision/epoch ACK and do not masquerade as live gain changes. The new live send command refuses changes if any routing draft is pending, a send is absent, device epoch changes, or a previous same-edge native ACK is pending. Native status `acceptedRoute.sends` is patched ONLY after the atomic target is acknowledged, so the next structural graph compile retains the live level. MAIN faders, plugin instances, MONITOR/OUTPUT1, Show state and route revision are not modified.

## Validation
Native existing CTest `ARMixMixerRoutingTests`: mono channel sent POST exclusively through AUX1; initial 0.25→0.5→0 transitions, 64-sample de-zipper, stabilized PCM, PRE/other AUX/channel/NaN/overrange rejection and immutable plan. Chromium G3/MS5 smoke: real JS emits live command without `routingDryApply`, accepted gain remains old until native ACK, ACK updates fader without changing the existing MAIN fader DOM or level; rejections remain safe; structural send creation continues through original MUTE/APPLY.

## Hardware FAT (still P0 #15 open)
Get the Windows artifact from the successful **exact-head** native Actions run, verify provenance/EXE hash, use same-interface wired ASIO/WASAPI 48 kHz and 44.1 kHz, start muted. Patch one mic to CH1 and existing stereo AUX1 return; load zero-latency reverb in AUX1 if compatible. In SENDS, create CH1 POST→AUX1 while output MUTED and APPLY once. Unmute conservatively, adjust SEND FADERS from -12 dB to 0 dB and back; hear reverb send change smoothly without crackles, click/pops or MAIN fader changes, and verify routing revision does NOT increment. Repeat with mute/solo, manual 0 level, device invalidation, keyboard, and Show save after stop. Record AUDIO HEALTH diagnostics and any glitch marker. **Passing CI never certifies physical zero-FX #15, 30-minute wired soak, measured latency or arbitrary VST vendor stability.**

## Not delivered by this milestone
PATCH2 two-click source selection, continuously updated PRE send/PAN, dynamic live CH11+, native MASTER FX, PDC, GROUP/DCA/MATRIX and factory VST3 binaries. Those remain on product M1/M2 gates. Avoid claiming full eMotion LV1 parity.
