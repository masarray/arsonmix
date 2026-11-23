# Vocal Reverb real-time engineering acceptance
Review: 2026-11-23, draft PR #887.

## Zero ADDED latency (not zero wet predelay)
The native AudioProcessor declares 0 samples processing latency and uses the existing primary JUCE PCM callback, no extra buffer, lookahead, ASRC, new clock, VST runtime or offline worker. Mix=0 must pass the first dry input sample at the same frame. Reverb early reflections and the deliberate 52 ms preset predelay affect ONLY the wet signal. End-to-end microphone ASIO latency depends on the sound device and complete FX chain.

## RT ownership and stable state
The original, attributed upstream ArSonVerb v1.0.1 JUCE-independent DSP is version pinned. The core prepares its delay rings, filter coefficients and modulation state outside the callback. The native adapter uses lock-free SPSC bounded settings and atomic audio-applied ACK/snapshots. A pending-count fence protects Show/.smix state against the race between FIFO pop and actual DSP application. Preset state is validated and restored BEFORE engine preparation, preserving the first-callback coefficient performance guarantee.

The adapter keeps its DSP/queue/state in VocalReverbProcessor.h and the message-thread-only GUI controls in VocalReverbEditor.h. No reverb-specific branch is added to the global mixer or I/O driver. This native effect is separate from the optional ArSonVerb VST3 plugin.

## Bypass lifecycle and efficiency
The existing host's completed bypass normally stops invoking the DSP and can freeze a non-empty reverb tail. The native adapter instead requests continuous HOST invocation and drains *silence* into the same FDN while bypassed, with the host returning its preexisting unaltered dry copy. No extra render graph or callback is created. The adapter stops computation once the tail has remained below 1e-6 for 0.4 seconds after at least 0.7 seconds, or on a bounded 0.7 + 2.5 x decay-seconds upper period. This is a perceptual low-tail threshold, not mathematical zero. Engine.reset() is deliberately NOT invoked from the realtime callback (it clears large delay buffers and could overrun tiny ASIO frames). Once tail drain retires, an idle bypass exits before expensive FDN computation.

## Objective test requirements
Actual JUCE adapter CTest: Mix=0 dry impulse at sample 0, Mix=1 delayed pure wet, 8 original vocal-presets' finite stereo tails and mono downmix, 44.1/48/96/192 kHz and 32 frames, pending-edit state guards, invalid state rejection, continuous full-bypass drain and no audible old tail on reopening. Existing pinned upstream C ABI test tests no allocations during Engine::process and segmentation invariance.

A separate, explicitly built *non-gating* target, ARMixVocalReverbCpuProfile, measures p50, p95, p99 and sample-deadline utilization across 44.1/48/96/192 kHz, 32/64/128/256 sample blocks and 1/4/8 serial DSP instances. Results are machine-specific and cannot promise 8 instances at 192 kHz/32 frames or all-hardware crackle-free ASIO.

## Musicality proof remains distinct
Reverb is not automatically superior to Alesis, FL Studio Reeverb2 or LuxeVerb because a spectral shelf is brighter. Owner audition at equal loudness should compare dense vocal plate, smooth hall, sibilance, metallic ringing, clarity of consonants, mono fold-down, level-matched tail, low-frequency masking and multiple vocal timbres. Measure impulse density and frequency-dependent RT60, then tune evidence-driven. Do not merge/release until exact-SHA Windows CTest and relevant operator/physical ASIO FAT pass; INT-1 #837 must stay unblocked.
