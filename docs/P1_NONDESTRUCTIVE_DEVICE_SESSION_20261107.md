# P1-A — Non-destructive Audio I/O session protection

**Integration owner:** PR #418 on `integration/daw-direct-hardware-input-20261105`. **Status: partial protection, NOT full device rebind or restart resume.**

## Causal correction

The prior `beginManualAudioSetup()` invoked `dryRoutingLab_.disable()`, releasing the accepted M4/ChannelId publishers, FX leases, WIRE graph and live controls on **dialog open**. `finishManualAudioSetup()` then created a fresh unpatched 10CH/4AUX graph even if the operator closed the selector without changing hardware.

The P1-A changes retain that accepted graph on setup entry, keep both outputs disarmed while device selection happens, and bypass default graph recreation when the accepted routing is still ready for the current hardware epoch. A driver change/failure preserves authored state **in process** but stops short of falsely claiming live PCM: the session remains unbound and muted until a future complete, verified rebind transaction is implemented. Other diagnostic/independent-clock paths now refuse to discard authored graph state without that transaction. This is containment, not full recovery.

## Explicit limitations / next critical engineering work

- The existing JUCE selector applies edits immediately; it is not a staged Cancel/Apply UX. A no-change close preserves the graph but leaves output muted for safety. Hardware changes require real identity, mask, sample-rate, buffer, FX preparation, graph publication and ACK validation.
- On an actual epoch change, the in-memory accepted logical graph and plugin lease data are **retained but not yet rebound or guaranteed to be fully visible**. Do not clear the mixer, fake a success ACK, use default endpoints, arm output, or call an M4-only Show recall to repair it. Restart does not yet restore this retained session.
- Before P1 can be marked complete, implement canonical MixerSessionDocument with durable stable IDs and complete FX state, safe DeviceApplyCoordinator, full dynamic graph rebind and GUI NEEDS REMAP projection.
- P2 crash-safe last-good generation, snapshots, plugin state blobs and restart resume remain pending.
- P3 timed VST3 catalog/cache and editor restore, P4 Inno Setup, P5 signed-update verification and installer qualification remain pending.
- Physical WASAPI/ASIO4ALL hardware FAT remains PENDING; green CTest cannot establish audible latency, sample-clock stability or sound quality.

## Regression evidence

The new C++ policy suite exercises all 8 verified/bound/authored combinations, asserting **createFirstSilentMixer** is legal only when no authored content exists. The policy is invoked directly by native `finishManualAudioSetup()`. A native MSVC build/CTest is required before any merge or qualification. This checkpoint does not claim full application acceptance.

## Next milestone: real two-phase rebind

1. Capture accepted immutable ChannelId model and original plugin/VST3 state on CONTROL (not inside callback).
2. Prepare a verified driver profile, compare physical input/output identities and explicitly mark changed ports NEEDS REMAP.
3. Rebuild graph with all authored stable IDs, buses, WIREs, original processing parameters, FX order/bypass and safe plugin state at the new sample rate/buffer.
4. Publish atomically, ensure accepted native readback and user-visible document projection. Keep output disarmed until an explicit ARM.
5. Rejected device or plugin preparation must not mutate the accepted document. Test no-change, active graph, ASIO4ALL WDM pin reorder, lost device, failed driver, rate/buffer changes, and concurrent editor/FX work.
