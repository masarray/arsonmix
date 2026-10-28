# P1F — JUCE hardware port binding to prepared native 10-channel graph

Source: main `15450e64d6fbb683150a624859a6351ca5ad2c34` (FX drag atomic reorder, Orbit/POLAR, meter, bypass and lean CI already reconciled). No edits to production `LiveAudioEngine.h`, `src/Main.cpp`, `web/*`, vendor VST or hardware settings.

## Implemented in code

- `engine/device/JuceActivePortSnapshot.h` reads the *real opened device's* `getActiveInputChannels()` and `getActiveOutputChannels()` masks on the control/device lifecycle thread. Sparse enabled physical inputs are sorted and mapped to JUCE callback pointer ordinals. Output physical indices are retained. Rejects missing device, unopened device, empty inputs/outputs, masks >256 physical channels, a changing mask while read, invalid epochs. No fallback to `input[0]` or invention of physical pins. Driver/ASIO registration identity remains protected by the existing direct-route fence.
- `engine/core/PreparedNativeMixerSession.h` composes `JuceActivePortSnapshot` + `PreparedRenderGraph` + `PreparedMixerBlock` + existing P1E `RealtimeRoutePublisher`. The **same** physical port mapping is validated into the DSP source and bound to immutable graph/device epoch/revision. Rejects invalid buffer limits and invalid routing without partial publication. Audio-reader render lease rejects changed device epoch or callback input/output counts and clears master output. Realtime render has no port discovery, allocations, device restart, file calls or locks. Snapshots and FX ownership are retired on control thread.
- Two executable native JUCE CTests, `JuceSparseActiveChannelMaskAndEpoch` and `P1FPhysicalPortToNativeMasterAndEpochGuard`, are built into existing Windows workflow. Test: active input bits 2/5/9, actual callback pointer order, channel physical mono IN9 + stereo IN2/5, CH1→CH3 return without physical input, exact stereo master audio values, stale epoch/input count/output count fail-closed, invalid routing rollback, and epoch/revision publication validation.

## Strict gate to full live DAW routing

This is **an executable control-to-DSP integration artifact**, not yet enabled as the native live JUCE host path. The existing host still starts `deviceManager_.initialise(1,2,...)`; enabling any other hardware input may require user-controlled device reopen. A verified requested physical ASIO mask and deliberate device profile must precede live activation.

The native callback currently owns one CH1-only VST chain; replacing it blindly would erase proven Orbit, FabFilter processing, FX reorder, anti-crackle bypass and the user's physical audio results. The next milestone must first provide independent VST3 instance ownership and real mono/stereo plugin bus negotiation, then wire a callback-boundary route change with ACK, preallocated FX scratch, actual VU per strip, and preserve DIRECT / Independent Clock diagnostics. Do not mark CH2–10 GUI buttons active without all of that.

Issue #15 zero-FX crackle remains open. Unit tests do not qualify hardware sound quality. Approved GUI remains unchanged; #115/#117 are overlapping offline graphs and must be reconciled by function rather than merged as duplicate engines.

## Acceptance and follow-up

Windows MSVC native CTest and UI must pass at exact PR head; no main merge without that. Audio interface ASIO tests with ports not starting at 1 and sparse enabled masks are required before declaring physical I/O mapping trustworthy. Device disconnect/reopen increments epoch and cannot inherit old live snapshots, with port identity remapping explicit for portable performances. If a route requests a currently inactive physical pin, the control plane must ask for Audio Setup and cannot silently reopen during live performance.
