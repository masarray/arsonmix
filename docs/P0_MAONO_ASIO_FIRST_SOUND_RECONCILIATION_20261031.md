# P0 MAONO ASIO First Sound — UI/engine reconciliation (2026-10-31)

## Evidence from operator screenshots
- A separate **Audio/MIDI Settings** window displays `ASIO / Maono ProStudio 2x2 ASIO`, enabled playback `Maono HP Out 1 + 2`, enabled capture `Maono VC In 3 + 4`, 44.1 kHz, 512 samples. This may be a *different host*: screenshot is NOT a JUCE ArSonMix device ACK.
- ArSonMix **CURRENT AUDIO DEVICE · NATIVE READBACK** continues to report `Windows Audio · CABLE Output (VB-Audio Virtual Cable) → Maono HP Out 1/2`, 48 kHz, 480 samples, `OUTPUT ARMED`. Selected ASIO `Maono ProStudio 2x2 ASIO` is explicitly a DEVICE DRAFT, not active.
- `APPLY DEVICE` is disabled with output armed; `MUTE OUTPUT` exists but the UI did not explain that its active route would continue running until a separate **explicit** device change.
- `NO PCM IN CURRENT BLOCK` is live input-meter evidence only. `NATIVE ROUTE ACK` is proof of topology, NOT of input samples, physical sound, driver synchronization, or the requested draft ASIO.

## Root cause and scoped P0 repair
This screenshot does **not** establish a PS22 firmware fault or a missing virtual channel. It establishes a **pending endpoint transaction** blocked by safety; audio from the older Windows Audio route can legitimately be silent if VB-Audio `CABLE Output` capture has no feeder, even while a different ASIO window lists VC input pins.

`web/mixer-io.js` now explicitly shows `ASIO DEVICE SELECTED · NOT ACTIVE` with actual backend, provides a shared switch blocker for the same button/action, explains MUTE OUTPUT vs secondary STREAM vs native route pending, displays the post-ACK hardware pin-selection stage for Maono ASIO (MIC vs VC 3+4) and never silently switches/rearms hardware. `tests/mixer_io_operator_contract_test.mjs` verifies this is not a disconnected visual-only flag and still relies on native `allOutputsDisarmed` and exact driver masks.

## Physical test order (not a software guarantee)
1. In ArSonMix Device/Driver, **MUTE OUTPUT**; also DISARM **SECONDARY STREAM** in SIGNAL CHECK if active. Keep headphone/amp level low. MUTE only disarms, it does not change the selected driver.
2. Choose ASIO → `Maono ProStudio 2x2 ASIO` → **APPLY DEVICE**. The previous Windows Audio / VB Cable route must cease being CURRENT in native readback. Native ACK shows the actual opened backend, rate, buffer and enabled pin names.
3. Under **BUFFER / SAMPLE RATE / HARDWARE PIN ENABLE**, explicitly select the valid Maono MIC inputs if testing a physical microphone, or the Maono VC In 3/4 pins if testing Maono's virtual feed, plus HP Out L/R. **APPLY DEVICE** again if pin masks changed. A mic signal does not automatically appear on VC In 3/4; source and MAONO internal routing must be checked. The source device and the output must be part of the same opened ASIO device, not an unrelated Windows capture endpoint.
4. In INPUT ROUTING select desired verified callback input source for CH01/CH02, native APPLY PATCH. Check the exact SOURCE→CH assignment and native revision. Then **START AUDIO** to request a separate hardware output ARM. Do NOT treat `NATIVE ROUTE ACK` or `OUTPUT ARMED` alone as incoming PCM.
5. If `NO PCM IN CURRENT BLOCK` persists, use SIGNAL CHECK/Copy Diagnostic Report and compare `rawCapturePeak`, callback input count, enabled pin masks and route source indices. Test MIC direct versus VC after checking the Maono Routing Center's virtual feed; do not blame the effect rack or dynamic Bus without evidence.

## Engineering status — what is and is not live
- M3A/B/C and M3D `DynamicAcceptedSession` were merged; `DynamicPreparedMixerBlock` is now PCM-testable with independent ChannelId/Group/FX return and rollback. BUT production JUCE callback still renders `LiveDryRoutingLab` 10CH+4AUX; M3E full dynamic Bus/FX/Show cutover is **not** released. Screenshots from a packaged executable may lag the latest commit.
- Existing P0 FAST1 provides one `START AUDIO` from native device, but must not auto-ARM on device change, to avoid unexpected acoustic amplification.
- #15 0FX crackle and device hardware FAT unresolved unless a measured Maono run confirms otherwise.
- Do not claim guaranteed zero-ms interface latency or automatic ASIO aggregation of VB Cable and Maono devices.

## Test strategy
Use existing Node mixer I/O contract and UI smoke workflow (small GUI-only PR), no new Windows JUCE build required for changed JS/documentation. If a native API bug is found, separately route a native P0 patch with its required Windows full CTest. Do not merge unrelated FX/Bus changes into this emergency hardware-workflow correction.
