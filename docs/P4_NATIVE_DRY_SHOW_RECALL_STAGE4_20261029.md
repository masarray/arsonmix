# P4 Stage 4 — Verified native DRY Show browse & recall (2026-10-29)

**Status:** Real native recall transaction for **zero-VST3** saved Shows only, not browser scene simulation or hardware FAT qualification.

## End-to-end operator path

1. User manually opens the correct DIRECT ASIO/WASAPI device, verifies visible physical input ports in Routing LAB, APPLYs an initial zero-FX route, and MUTEs physical output. There is **no automatic hardware device selection** from saved files.
2. **BROWSE SHOWS** runs a bounded metadata walk of `Studio/Shows` on the same owned IO worker as SAVE, reading at most 256 Show directories and returning the most recent 64. No filesystem work or SHA hashing on the JUCE callback/GUI paint thread. The browser displays native IDs/names, never untrusted disk paths.
3. **RECALL SELECTED DRY** accepts a strict 36-character UUID, captures the current device epoch+graph revision token, and asks the worker to call the existing SHA-verified `NativeShowStore::load()`. Full manifest, ordered snapshots, AUX, referenced VST3 blob hashes, and bounded generation integrity are checked on read before the snapshot is returned.
4. After IO completes, on the JUCE message thread the app rereads native status and rejects stale epoch/revision, deactivated Routing LAB, unmuted output, independent clock, missing/changed physical input ports, or active VST3.
5. `NativeDryShowRecall::prepare()` decodes/revalidates schema V3, rejects any CH/AUX VST3, unsupported master pan / gain, and produces a **new** `NativeDryRouteCommand` containing actual 10 INPUT channel routing, four dedicated AUX, fader gains including exact zero, PAN/MUTE/SOLO, PRE/POST sends, BUS vs NONE, and master routing. The preflight does **not** publish, load a plugin, touch a hardware device, or mutate caller output on failure.
6. Only the existing `LiveAudioEngine::applyDryRoutingLab` can compile and publish the new immutable prepared graph. Its single-clock DIRECT gate, epoch+revision CAS, hardware sparse-mask validation, zero-VST condition, and RT snapshot retirement remain authoritative. On success, master linear gain/mute/mono apply on the control thread, with hardware output **still MUTED**. Failed preflight or publish leaves the prior route unaffected. Dedicated MASTER MONO is now honored by the native Routing LAB callback: zero-allocation 64-sample stereo-to-mono ramp while unmuted, primed to exact mono while hardware output is muted. MASTER controls are synchronized through one native bridge helper for both CH1 and LAB paths; prior LAB early return may no longer skip the master fader/MUTE/MONO state.
7. GUI shows explicit native ACK/rejection and refreshes routing draft from the *new* native acceptedRoute. **UNMUTE is disabled during any Show IO/USB backup**, preventing disk activity from racing an intentional live output unmute. User must verify input mapping at low headphone volume before explicitly unmuting.

## Architectural constraints

- One existing JUCE audio engine, no competing mixer, no callback alloc/lock/I/O/JSON, no third-party VST lifecycle calls in recall.
- Persisted physical port indexes are DEVICE-LOCAL. Existence on the newly selected device is verified, but matching numeric indexes alone are **not** proof of matching microphones: never auto-unmute, manually verify hardware identity/routing on another computer.
- VST3-equipped Shows are **fail-closed** even if all binaries/hashes exist. Correct vendor `getStateInformation` thread affinity and quiescence, cross-vendor state restore, latency/PDC, Orbit state and backup licensing remain future gates.
- The live Routing LAB remains an explicit engineering hardware mode; this milestone is not yet a production-scene playback guarantee.
- A pending Show load checks the captured epoch/revision again after IO; hotplug/STOP/another APPLY cannot publish stale recorded ports. The user may always STOP and keep output muted.

## Acceptance

- Native CTest covers end-to-end schema3 on-disk roundtrip, then live preflight conversion (10+4, BUS/NONE identity, sparse physical ports, hard linear zero, CH and AUX PRE/POST, master); rejects missing port, unmuted output, invalid current graph state, vendor VST3 and unsupported master gain without changing previously prepared command.
- Chromium UI smoke must pass for the new native browse/recall controls and event bridge.
- Windows MSVC must rebuild actual JUCE executable and pass all native CTest + embedded UI ZIP integrity. No CI run should be started until this bounded change is ready.
- On actual hardware require manual FAT for DIRECT ASIO/WASAPI, 1/2/10 INPUT, 4 AUX, 128/256/480 frames, STOP/replug, accidental unmute prevention, port identity, readback, zero FX and crackle #15. CI does **not** qualify real hardware.

Related: #137, merged #138 (AUX Show store), merged #139 (live SAVE), #15 zero-plugin crackle.
