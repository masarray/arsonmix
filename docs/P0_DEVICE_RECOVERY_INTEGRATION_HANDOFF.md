# P0 backend recovery handoff — generation-safe control plane

**Updated:** 2026-10-25  
**Reference:** [P0 issue #15](https://github.com/masarray/arsonmix/issues/15), [ARD v1](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), [Smart Audio implementation blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md)

## Implemented in this engineering tranche

- Pure `DeviceRecoveryPolicy` state machine for the message/control thread. States: stopped, discovering, preparing, running, degraded, recovering, failed. Every prepare/discovery completion must include the generation issued by its start. A late completion for a retired device is rejected rather than re-arming invalid PCM pointers.
- On endpoint loss: require render silence and device retirement **before** acquiring replacement. Retry uses bounded backoff 100 ms–10 seconds, maximum eight consecutive failures, and explicit terminal failure.
- A configuration is **eligible** for stable persistence only after 30 seconds of healthy callbacks. This is a policy primitive, not a claim that audible playback is clean.
- Pure `SplitStreamTelemetry`: lock-free independent capture/render frames, dropped input frames, bridge starvation frames and invalid-buffer counters, with separate generation numbers. These are not equivalent to Windows driver output xruns; report source provenance.
- `IndependentClockPipeline` now connects two **independently invoked callback interfaces** to the prepared FIR/ASRC and telemetry. It is JUCE-independent and tested against separate synthetic source/render callback frame counts.
- `JuceIndependentDeviceAdapter` now implements two actually separate JUCE AudioDeviceManager callback hosts: one input-only clock, one output-only clock, capture FIFO producer, render-master FIR consumer and bounded render blocks. Its source compiles into the Windows app and an explicitly opt-in **EXPERIMENTAL 0-FX** route activates separate one-sided JUCE device managers using controlled joined-device shutdown, bridge preparation and rollback. It is NOT a general plugin-host backend, nor hardware-certified. Independent capture rate is negotiated separately; output uses selected render period.
- Release-active CTests for generation cancellation, state transitions, bounded delays, stable eligibility, recording different capture/render frame rates, and FIFO overflow/starvation/re-prime.

## Production integration is **not yet complete**

The `DeviceRecoveryPolicy` planner is not yet wired to runtime AUTO/STABLE control. The default JUCE joined route still cannot expose independent clocks; the optional Smart I/O 0-FX path uses separately opened capture/render device managers and reports real bridge frame starvation. The true P0 remaining acceptance scope is:

1. Replace the current manual Smart I/O 0-FX opt-in with capability-driven AUTO/STABLE selection and a device-identity/period evidence provider. The first adapter has independent callbacks but no Windows IAudioClient3 period/clock service verification or event-driven render-padding instrumentation.
2. Extend the split route beyond zero FX through a safe, independently prepared VST3 graph handoff and ensure direct ASIO/shared-clock fast path remains free of extra SRC. The 25 ms bridge target is experimental, NOT a universal low-latency claim.
3. Attach this `DeviceRecoveryPolicy` to a real control executor that serializes COM/device change transitions, invalidates and drains old callbacks, safely owns graph/plugin lifetimes, and makes stable profile persistence transactional.
4. Integrate `SplitStreamTelemetry` into native **COPY FULL REPORT**: actual split route now exports FIFO occupancy, source frame drop, bridge underflow/priming frames, and ppm; real driver render underruns remain unavailable. Host callback timing is not an xrun.
5. Add deterministic rate-mismatch jitter and device restart failure tests plus resampling fidelity/latency/CPU acceptance gates; run automated Windows/MSVC and GUI checks.
6. Perform one consolidated hardware acceptance campaign after P0 stages are integrated. Until then mark P0 #15 OPEN and physical crackling **unresolved**.

**User workflow:** no recurring manual tests while the engineering work proceeds. An experimental **ENABLE SMART I/O · 0 FX** control is in the native SIGNAL CHECK overlay only, and COPY FULL REPORT includes its actual activation state. This control is *not* the final AUTO/STABLE selector. The approved mixer GUI v0.8 remains locked.
