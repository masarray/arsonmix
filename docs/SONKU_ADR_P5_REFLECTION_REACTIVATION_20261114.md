# SonKu P5 — directional ROOM reactivation uses only fresh history
2026-11-14 · draft PR #674 · experimental native room only

## Verified source-level risk
SonKuEarlyReflections::setRealtimeRoom resets `validFrames_`
when a previously inactive ROOM is turned ON. The previous
lightweight `delayed()` code respects `validFrames_`,
but the new P2/P4 measured-HRIR 140-tap convolution read
directly from `history_`, whose ring entries contain samples
from the previous listening epoch. In a quick OFF/ON cycle,
old audio could become false *new* directional reflections.
Blindly clearing two 8192-float delay rings during control
updates on the audio thread would create a potentially blocking
64KiB memory-write burst and harm realtime performance.

## RT-safe solution
When `realtimeMode_` is enabled, compute the count of valid
reflected FIR taps from this activation:
`freshTaps = clamp(validFrames - integerDelay, 0, 140)`.
Each reflection FIR tap requires the two adjacent fractional
delay samples; therefore if k>=freshTaps its input is zero,
and cannot read stale ring values. Continue to slew both
KEMAR ear kernels for all taps during warmup so source
movement and smoother state are maintained. Once history
has filled the delayed 140-tap window, the full exact 140
tap processing remains unchanged. No heap, extra state
arrays, locks, I/O, secondary device or ring clear.

The previous approved light renderer, and offline P2/P3/P4
exact-native fixtures with `realtimeMode_ == false`, remain
unchanged; no native GUI, FX identity, .smix schema, ACK,
source routing or hardware release change.

## CI acceptance
44.1/48k, four 7.2m first-wall configurations:
feed independent 440/720Hz stereo while room on, then keep
feeding 520/1250Hz while ROOM fades to OFF so the old ring
definitely contains audio. Re-enable ROOM on silence: every
sample must be exactly zero. New source impulse afterwards
must produce nonzero causal reflections; the warmup may
not permanently mute the processor. Repeat for source
spread 0/60/120 and P2 full vs P4 contiguous.
Existing full P0/P1, P2/P3/P4 native/spectral/CPU checks
must continue to pass. P5 does not establish subjective
externalization, nor is it an ASIO hardware test.
