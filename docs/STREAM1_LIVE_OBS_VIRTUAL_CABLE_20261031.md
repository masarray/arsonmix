# STREAM1 — Live OBS Virtual Cable Output (2026-10-31)

## Product direction and practical workflow

ArSonMix is a realtime virtual live mixer for karaoke, podcast, radio, streaming and live performances. It is NOT an audio editor or DAW. Studio One and eMotion LV1 are mixer GUI, bus/FX, workflow and reliability references.

Signal topology: ASIO/WASAPI capture → one native channel/FX/AUX/MAIN mixer graph → physical MAIN/MONITOR destination AND an explicitly selected independent WASAPI playback endpoint (virtual cable). The virtual cable RECORDING endpoint is then selected in OBS Audio Input Capture. A second output clock is NOT a second DSP graph.

## STREAM1 source implemented

- LiveVirtualCableOutput is a render-only JUCE/WASAPI output callback, with no inputs, no DSP/FX plugin instances and no separate mix engine. It uses existing bounded AdaptiveClockBridge FIR/ASRC and ClockBridgeSizing to bridge the original physical-device clock to an independently scheduled playback-clock consumer.
- Native Core Audio IMMDevice endpoint identity is validated before and after selection. Output is exactly stereo and no input; the same physical speaker name or an ambiguous duplicate endpoint is rejected, as is driver substitution.
- The existing LiveAudioEngine processes MAIN PCM once and publishes the post-FX/post-main gain/post-declick L/R exactly once to the bridge. Physical monitor mute can be OFF while stream output is explicitly ARM-ed. Channel/MASTER mute, route reconfiguration, stale direct device, diagnostic modes and missing native routing gate both destinations.
- The STREAM1 UI is a compact part of existing Signal Check, not a new view or timeline. It exposes actual Windows Audio output devices, CONNECT, explicit ARM/DISARM, DISCONNECT, native ACK, selected endpoint, sample rate, ASRC target buffering and FIFO overflow/underflow measurements. No auto-start, auto-unmute, or hardcoded virtual cable brand.
- The existing AdaptiveDualClockBridge CTest gets additional gate and streaming-clock tests; CMake reuses its existing target (no extra CI job).

## Engineering truth and release gates

Dual clock paths cannot be mathematically zero latency. The FIR ASRC needs finite buffers, initially targeted at 24ms and increased according to actual driver buffer periods. Report it as added streaming bridge latency. Monitoring on one direct device remains the zero-extra-FIFO path. No measured round-trip latency or OBS glass-to-glass latency claims without hardware tests.

This is one additional stereo output destination, not an internet radio encoder, streaming server or DAW editor. Users install a real virtual cable and configure OBS to capture the corresponding recording endpoint. Avoid OBS duplicate desktop/cable capture and cable monitoring feedback loops.

Test actual ASIO input + monitor + OBS cable at common sample rates and driver buffer sizes, including physical output muted while stream armed, device hotplug, cable removal, ASRC underflow/overflow, and VST3 state. Unplug or identity change must require explicit reconnect; Show recall never arms stream automatically. Release blocker #15 real zero-FX crackle remains open.

Concurrent MS3B #203 touches the native MAIN/MONITOR render callback and must be reconciled with STREAM1 before simultaneous merge. DYN1B remains needed for live CH11+, and DYN2 for dynamic channel add/remove GUI. No false Studio One/LV1 parity claims.
