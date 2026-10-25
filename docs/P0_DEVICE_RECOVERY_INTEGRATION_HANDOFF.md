# P0 backend recovery handoff — generation-safe control plane

**Updated:** 2026-10-25  
**Reference:** [P0 issue #15](https://github.com/masarray/arsonmix/issues/15), [ARD v1](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), [Smart Audio implementation blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md)

## Implemented in this engineering tranche

- Pure `DeviceRecoveryPolicy` state machine for the message/control thread. States: stopped, discovering, preparing, running, degraded, recovering, failed. Every prepare/discovery completion must include the generation issued by its start. A late completion for a retired device is rejected rather than re-arming invalid PCM pointers.
- On endpoint loss: require render silence and device retirement **before** acquiring replacement. Retry uses bounded backoff 100 ms–10 seconds, maximum eight consecutive failures, and explicit terminal failure.
- A configuration is **eligible** for stable persistence only after 30 seconds of healthy callbacks. This is a policy primitive, not a claim that audible playback is clean.
- Pure `SplitStreamTelemetry`: lock-free independent capture/render frames, dropped input frames, bridge starvation frames and invalid-buffer counters, with separate generation numbers. These are not equivalent to Windows driver output xruns; report source provenance.
- `IndependentClockPipeline` now connects two **independently invoked callback interfaces** to the prepared FIR/ASRC and telemetry. It is JUCE-independent and tested against separate synthetic source/render callback frame counts.
- `JuceIndependentDeviceAdapter` now implements two actually separate JUCE AudioDeviceManager callback hosts: one input-only clock, one output-only clock, capture FIFO producer, render-master FIR consumer and bounded render blocks. Its source compiles into the Windows app but **is not activated**: a controlled migration from the existing joined device and plugin-graph ownership must be implemented and verified before exposure. Independent capture rate is negotiated separately; output uses selected render period.
- Release-active CTests for generation cancellation, state transitions, bounded delays, stable eligibility, recording different capture/render frame rates, and FIFO overflow/starvation/re-prime.

## Production integration is **not yet complete**

The policy is intentionally not wired to the legacy JUCE joined audio device callback or its GUI yet. The existing app still cannot measure independent capture/render clocks from that callback. The true P0 remaining acceptance scope is:

1. Implement independent Windows capture and render stream adapters or demonstrate that a supported JUCE API exposes actual independent timelines. Use real endpoint IDs/formats/IAudioClient buffer events, not friendly-name matching.
2. Wire the prepared `AdaptiveClockBridge` only for **proven independent clocks** with render-master timeline, bounded callback scratch buffers and no RT allocations. Maintain direct ASIO/shared-clock fast path.
3. Attach this `DeviceRecoveryPolicy` to a real control executor that serializes COM/device change transitions, invalidates and drains old callbacks, safely owns graph/plugin lifetimes, and makes stable profile persistence transactional.
4. Integrate `SplitStreamTelemetry` into native **COPY FULL REPORT** with true callback-source labels; do not claim render driver starvation from inferred callback delays.
5. Add deterministic rate-mismatch jitter and device restart failure tests plus resampling fidelity/latency/CPU acceptance gates; run automated Windows/MSVC and GUI checks.
6. Perform one consolidated hardware acceptance campaign after P0 stages are integrated. Until then mark P0 #15 OPEN and physical crackling **unresolved**.

**User workflow:** no recurring manual tests are required while this engineering work proceeds. The approved GUI v0.8 baseline is locked; existing SIGNAL CHECK diagnostics remain unchanged.
