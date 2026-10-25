# P2.5.1 — Live Preset Graph Handoff (ARD P2 safe-recall foundation)

## Product goal
Previous P2.4 recall required MUTE OUTPUT and called JUCE removeAudioCallback/addAudioCallback around graph changes. This interrupted the hardware path.

## Implemented
- The existing Add FX FabFilter/RX compatibility and P2.4 .armix ZIP format remain unchanged.
- Preset recall validates every archive entry, recreates all VST3 processor instances, restores each vendor binary state, and prepares DSP outside the realtime callback while the old graph continues to process audio.
- A bounded **one-slot publication** object exposes an immutable prepared Graph pointer at a buffer boundary. RT thread does no plugin creation, graph copying, mutex locking, dynamic allocation, or third-party destruction during handoff.
- In the last old callback the output gain ramps downward over one buffer; new graph is activated at callback boundary. In the first new callback output ramps upward. No double-DSP old/new crossfade occurs: avoids worst-case CPU doubling with expensive RX/FabFilter processors.
- Audio callback publishes the retired pointer. JUCE message timer takes acknowledgment and destroys old plugin instances only after old callback is finished, then announces successful recall to UI and updates Channel 1/FX Rack controls.
- Snapshot fader/PAN/mute/master settings and VST3 latency are updated at graph transition. Hardware input/output device is untouched.
- New stress test executes 12,000 cross-thread graph handoffs and validates no early reclamation, no second pending graph and expected fade endpoints.
- Counters `completedGraphSwaps` and `graphSwitchPending` are added to status and 1-Hz diagnostics.

## Precisely what is NOT complete
- This is **buffer-boundary declick switching** with a roughly two-buffer gain dip, not genuinely seamless dual-run or delay-compensated crossfade. Effect tails do not persist through graph replacement.
- Add/Remove/Reorder FX still detach the callback and can cause a brief glitch; graph structural edits need separate migration.
- Plugin preparation and ZIP reading remain synchronous on the message thread, so UI may freeze temporarily although audio callback remains independent. Next phase must stage on background worker, coordinate JUCE plugin threading requirements and deal with plugin state APIs that require message-thread affinity.
- Simultaneous audio device change while a graph is pending should be avoided until device-lifecycle cancellation is implemented.
- Plugin compatibility and xrun telemetry must be verified with real FabFilter, iZotope RX and ASIO hardware before declaring live-ready.

## Windows physical acceptance
1. Load FabFilter (e.g., Pro-Q) and iZotope RX, confirm both work; save as `Chain A` (P2.4 save still requires output muted). Repeat with a second preset `Chain B` containing a different sound.
2. Unmute output and play continuous signal. Open MY SOUNDS and RECALL LIVE alternating presets while listening on headphones.
3. Check graph count increases in SIGNAL CHECK diagnostic status; ensure the audio device never restarts, current and new plugin editors open correctly, and saved vendor states restore.
4. Compare interrupted signal length and xrun timing with old P2.4 and review AUDIO_HEALTH after 2 minutes. Avoid device changes during recall.
5. On any stuck load or stream loss, share `audio-diagnostics.log` and stop using the experimental release for a real broadcast.

## Release gates
- Windows x64 MSVC, JS contract, CTest stress all green.
- Physical 30+ minute soak and plugin-specific AB listening (FabFilter, RX, ArSonKuPik) still required. Draft PR only.
