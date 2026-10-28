# P1E — REAL 10-CHANNEL DRY ASIO/WASAPI FAT LAB (2026-10-28)

**Native code, not just another offline graph.** The owner-approved ten-strip P1D `PreparedMixerBlock` is now callable from the live JUCE audio callback in a **separate, explicit, fail-closed DRY ROUTING LAB**. This milestone exists to prove **actual physical input channel separation, mono/stereo hardware patching, routing graph activation and Windows device continuity**, while preserving the current VST3 CH1 production path whenever the lab is OFF.

**The lab is not a finished performance mixer.** It deliberately has NO VST3 inserts, AUX sends, independent channel fader binding, automatic portable device map restore, multi-output patchbay, PDC or FX Tail guarantees. CH2–CH10 rack buttons remain truthful preview. Never advertise ten live VST3 chains or approved #15 zero-FX crackle status based on this PR alone.

## Open it on real Windows hardware

1. Build Windows artifact from **the passing P1E PR head**, do NOT use a stale previously downloaded build. Set headphone volume physically LOW.
2. In **AUDIO SETUP**, choose and verify the actual ASIO/WASAPI DIRECT interface and enable the needed physical input channels in its active input mask. Do not expect `initialise(1,2)` to activate all channels automatically.
3. If a VST3 was loaded in CH1, save/stop it and remove it from the chain before the dry test. Disable Smart Clock / independent capture/render, set SIGNAL CHECK to NORMAL, and leave the verified DIRECT route selected.
4. Open **SIGNAL CHECK**, click **MUTE OUTPUT**, then **ROUTING LAB · 10 CH**. Only channels reported from the **actual opened device's enabled input-channel bitmask** appear in the selectors. If the port you need does not appear, enable it in AUDIO SETUP, which may restart the device; do not lie with phantom dropdown ports.
5. Example with an active 8-input interface: set CH1 **MONO** input 3; CH2 **STEREO** input 5(L) / 6(R); CH3 **MONO** input 1. All other CH4–CH10 **NONE**. Choose a low initial per-strip level, e.g. −18 or −12 dB. Press **APPLY TO NATIVE AUDIO**; wait for explicit native ACK, never infer audio commit from UI selection alone.
6. Close the routing lab, go back to SIGNAL CHECK, and deliberately **UNMUTE OUTPUT**. Stimulate each physical input separately and confirm that only the corresponding path is heard. Then try nonadjacent L/R mappings, duplicate input fanout and a muted/None channel.
7. MUTE OUTPUT and press **STOP LAB**. STOP also forces output mute before returning to the original CH1 VST3 engine; unmute only after selecting/validating the wanted signal path.

### Safety and determinism

- The lab has a separate exact input registry: `juce::BigInteger activeInputChannels` is enumerated **off callback**, mapping actual hardware ID to JUCE's packed callback pointer index. Callback channel count is checked against the prepared map for every buffer. Max 256 enabled pins accepted; larger counts fail closed.
- A monotonic **device epoch** invalidates prepared routes on device stop/restart. An active lab with a stale device does NOT fall through to old CH1 audio; it outputs silence until the operator explicitly reapplies or stops the lab. UI shows **DRY LAB STALE — SILENT**.
- Graphs compile on the message/control thread; a single atomic pointer is published with `expectedRevision` and `deviceEpoch`. Callback obtains a no-heap lease before accessing the prepared graph. Old buffers are retired only when all callback readers leave. Excess rapid edits receive a specific retry response instead of freeing audio-owned memory.
- Channel processing is bounded, uses P1D's preallocated fixed workspace, has no VST instantiation or lock in the audio callback. Variable callback quanta are chunked into the prepared hard-frame limit. On invalid buffer or stale epoch, output must be silence.
- An initial 256-sample bounded de-click fade on a new route revision starts at the previously emitted output sample. This is a topology-switch mitigation, **not yet proof of click-free behavior at any buffer rate/driver**.
- MUTE OUTPUT is a *required precondition* of every APPLY; there is never an automatic audible route to an unknown speaker. STOP forces output mute. FX add/remove, preset recall, Smart Clock mode and other diagnostic modes are blocked while LAB is active.
- The lab sends dry output to actual render callback channels 1 and 2 (0-based indices 0/1); additional output channels are cleared. No hidden ASRC/split-clock buffering is introduced on a direct ASIO path.
- If the selected input is no longer available, no plugin can synthesize signal from a dummy channel and callback output remains silent. No route is persisted to a future machine until endpoint/physical-port identities have been verified by a portable-source migration workflow.

## Engineering validation

`ARMixLiveRoutingEpochTests` includes sparse 5/2/9 input mapping, duplicate/invalid masks, generation mismatch, revisions, ABA-safe reader-held graph lifetime, full retirement queue and explicit STOP fail-closed, 8,000 sequential publications and 1,000 concurrent read/publish cycles, with all C++ assertions enabled even in Release.

`tests/live_routing_ui_contract_test.mjs` asserts the opt-in event, status/ACK, mute requirements, user port selector and preservation of the original non-native VST3-preview strips. Existing Windows native build compiles the P1E JUCE header and all previous native realtime/Orbit/FX tests; Node checks validate monitor.js.

**Real-device acceptance checklist (NOT YET PASSED):** ASIO 2/4/8 inputs; non-contiguous enabled active mask; mono→both speakers and stereo L/R isolation; unplug/replug and driver re-selection silences until remap; rapid edits/STOP while audio runs, no heap/click, 0 VST case compared with issue #15; 48/96 kHz, short buffers and controlled low-volume headphones; compare DIRECT to independent-clock diagnostics only outside LAB. Preserve logs for genuine device/render errors.

**Merge gate:** exact-head Windows Native CTest + Chromium smoke, inspect binary artifact/provenance from Actions, reconcile all parallel PRs against latest `main` before merge. **Native GUI faders for CH2–CH10 and channel-VST3 processing are not delivered in this PR.** Follow-on P1F must implement per-strip plugin instance ownership, negotiated mono/stereo bus layouts and authentic mixer fader/FX bindings after the 0-VST3 hardware signal-path FAT is clean.
