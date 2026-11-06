# P0 live routing recovery — CH03 → CH04 and SOURCE ONLY

Continue PR #418 on `integration/daw-direct-hardware-input-20261105`.
The approved assembled `/preview/` appearance and sole JUCE callback remain.

## Root causes and changes

1. `integration/ApprovedNativeControlIntent.h`, the decoder actually included
   by `Main.cpp`, rejected INPUT destinations. The PCM compiler already
   supports channel-to-channel routing. Admit an INPUT only when both the
   accepted public and native inventories identify that exact ChannelId with
   the same role. Retain epoch, graph, roster, edge, feedback and source guards.
2. `DynamicPreparedMixerBlock` culled every lane with no path to MAIN or
   MONITOR. SOURCE ONLY therefore lost its DSP and local meter. Schedule
   physical/insert signal roots and their descendants independently of output
   reachability. Empty, unpatched descriptors still use no audio scratch.
3. Publish INPUT channels in the accepted destination inventory. Preserve
   detailed native WIRE rejection reasons through the existing IPC executor
   and ACK, including feedback cycle, duplicate route and stale edge.

## Defined audio behavior

- Physical capture and incoming WIRE PCM **sum before** the receiver's mono
  fold, inserts, fader, PAN, MUTE/SOLO and post-fader meter. A hardware selector
  set to None leaves incoming WIRE audio intact.
- CONNECT adds a deliberate parallel post-fader send; it can produce a second
  audible path when the source still has a primary MAIN route. ROUTE ONLY
  replaces the primary destination and preserves deliberate parallel sends.
  Promotion of an existing send retains its EdgeId and does not double dry PCM.
- SOURCE ONLY removes the primary edge; source capture, inserts, meter and
  deliberate parallel sends remain active. Removing an EdgeId affects only
  that edge. Feedback cycles reject the complete candidate atomically.

## Regression evidence

`P0ApprovedDecoderChannelToChannelAndSourceOnlyPCM` is pulled into the existing
Windows build through `ARMixMixerRoutingTests`, then executed by CTest.
It crosses the approved production decoder/executor, accepted session,
publisher, compiler and renderer, asserting actual sample values and meters:

- CH03 stereo hardware → MAIN; CH07 mono remains independently audible.
- CH03 → CH04 with no hardware on CH04; receiver FX gain and fader affect PCM.
- Parallel send, promotion to exclusive primary with stable EdgeId, unplug,
  reconnect, retarget and CH03 → CH04 → BUS → MAIN.
- SOURCE ONLY at source or receiver retains local DSP and meters.
- Live receiver fader/PAN, FX seam bypass, MUTE/SOLO; hardware plus WIRE summing.
- Cycle rollback, stale graph/epoch and forged native destination role reject.
- Zero C++ heap allocations during every tested render block.

The new regression fails with the old decoder and, separately, with the old
render scheduling. Existing routing/dynamic PCM tests, approved native v8
tests, 127 fast native contracts, and deterministic approved GUI provenance
checks also pass locally. The Chromium/Windows result and exact checkpoint
SHA are recorded in PR #418; do not infer those results from this document.
The test FX seam is deterministic DSP, not a third-party VST3 hardware test.

## Short physical FAT

1. Use the checkpoint's **UNQUALIFIED** Windows ZIP. Choose a real ASIO/WASAPI
   capture/playback pair. Set CH03 IN to stereo 1+2, CH04 IN to None, both
   faders to 0 dB; no MUTE/SOLO. Verify CH03 and MAIN activity first.
2. Connect CH03 OUT → CH04 IN in normal post-fader send mode. CH04 meter must
   move. Remove CH03's direct MAIN edge for a serial listening test; otherwise
   the retained parallel MAIN route deliberately adds another dry contribution.
3. With CH04 → MAIN, move CH04's fader and bypass/enable a real VST3 insert.
   Its output must affect MAIN. Test MONO/STEREO and MUTE/SOLO.
4. Remove CH04 → MAIN: CH03 and CH04 meters must keep moving; their contribution
   to MAIN stops. Reconnect without changing hardware IN or restarting audio.
5. Set CH03 to SOURCE ONLY with no other sends: CH03's meter remains active,
   CH04 no longer receives it. Reconnect and test BUS/MAIN plus device reopen.

On failure, immediately choose **More → Copy Audio Report** and paste the
report with the exact ZIP/source SHA and routing steps. Physical listening,
vendor VST3 behavior, ASIO timing, hotplug and crackle soak remain **PENDING**
until observed on the user's hardware. This checkpoint does not authorize
merging main or promoting a stable release.
