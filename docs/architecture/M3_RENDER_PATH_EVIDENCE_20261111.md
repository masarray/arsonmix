# M3a — production render-path evidence

## Verified callback structure

The one JUCE device callback is LiveAudioEngine::audioDeviceIOCallbackWithContext.
It checks the hardware direct fence, then NativeRenderAuthority decides
fail-silent unpatched, accepted routed session or isolated diagnostics.
A routed session calls LiveDryRoutingLab::process ONCE. Its nonblocking
RealtimeRenderPermit prevents overlapping callbacks from invoking the same FX.

Inside this existing sole routed pass, native ChannelId PCM uses
DynamicNativeSessionPublisher::render when the matching device-epoch admission
is installed. The compatibility branch uses PreparedMixerBlock::process only
when no native ChannelId PCM epoch is admitted. These are alternatives, never
summed, but M4 compatibility is still reachable in source. Do NOT delete it.

The explicit diagnostic tail runs no vendor VST3. SOURCE ONLY may still
process and meter channel PCM without a MAIN signal, and hardware mute can
silence device pins while a logical graph continues to run. The secondary
WASAPI output copies MAIN PCM after this single render pass.

## Instrumentation and meaning

The native status object now contains dryRoutingLab.renderAudit. It reports
nativeStarted, nativeCompleted, nativeFailed, m4Started, m4Completed, m4Failed,
overlapDenied, unpatchedSilent and mutedNoRender. All counters are monotonic
within the application process, not per device. They are independent relaxed
atomics: a concurrent CONTROL snapshot can briefly observe an unfinished
callback attempt. A block is counted once regardless of DSP chunk count.

This is an observation, not output authorization or proof of audible sound.
Compare deltas of these counters across a field-test window alongside
deviceEpoch, routing admission and output mute. Do not add browser polling
or a new dashboard solely for these metrics.

## Tests and next-step admission

The new zero-JUCE M3ActualProductionRenderPathAndFailSilentCounters CTest checks
that native and M4 attempts, completions, failures, overlapping callbacks and
silent exits remain distinct. Existing real block-by-block PCM tests for
WIRE, SOURCE ONLY, parallel sends, bus and channel FX remain mandatory.
A Windows MSVC/full CTest validates integration. Physical ASIO listening is
PENDING and cannot be inferred from green CI.

Retire an M4 branch only after actual native field sessions show no M4
render deltas with equivalent VST3/FX, channel meters, bus and physical MAIN
performance across restart, devices and output mute. Never permit legacy
fallback merely because native publication failed.
