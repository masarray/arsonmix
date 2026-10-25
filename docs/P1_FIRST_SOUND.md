# P1 FIRST SOUND — Windows VST3 host
**Status:** Experimental source implementation. Windows physical I/O and VST3 compatibility must be confirmed by CI and hardware testing.

## Hardware trial
1. Download the build artifact **AR-MIX-Studio-P1-Windows-x64** from the P1 GitHub Actions run.
2. Extract the ZIP. Install the Microsoft WebView2 Runtime if not already present.
3. Connect an audio interface and use wired headphones; reduce headphone/output volume.
4. Launch the EXE. Output starts **MUTED** deliberately to prevent feedback.
5. Click **AUDIO SETUP** at the bottom status area. Select ASIO when supported, otherwise Windows Audio/WASAPI. Set microphone input, headphone output, 48 kHz, and begin at 128/256 frames.
6. Click **LOAD REAL VST3** and choose one compatible installed stereo `.vst3`. The visual FX rack is still a DEMO: only the actual path chosen in the native strip is loaded.
7. Click **MONITOR ON**. Speak into the microphone and check headphone audio; test bypass. Lower physical gain if clipping or feedback occurs.
8. Open the native audio setup to try 64 frames *after* confirming clean audio at 128/256. Log pops/dropouts and real device latency.

## Scope
- Native JUCE AudioDeviceManager (ASIO and Windows Audio/WASAPI), live input->output, one effect VST3, device dialog, monitor arm/disarm, bypass, master gain, basic callback-driven meter.
- No per-channel DSP beyond the first live stereo path. Track 2–12 and mock FX rack still simulate UI data, visually dimmed native meter.
- No plugin editor, multichain, host automation, plugin sandbox, latency compensation, snapshot state restore, streaming loopback, or crash isolation yet.
- Plugin insert/removal momentarily detaches audio callbacks. This is safe for ownership but **not glitch-free**. Do not change plugin on air.
- JUCE WebView2 UI does not process real-time audio. The audio callback avoids host-side heap allocations, locks, logging, file I/O and WebView access. Third-party VST3 code may violate RT safety and cannot be trusted.
- Host buffer period in ms is **not** actual input-to-output round-trip latency. Measure hardware loopback separately.

## Known P1 risks
- Third-party VST3 discovery/creation runs on the message thread and may block or crash with badly-behaved plugins. P2 must isolate scanning and implement verified safe graph transitions.
- The current stereo-only effect restriction intentionally rejects mono-to-stereo and instrument-only plugins.
- ASIO and WASAPI hardware availability varies. Select a single full-duplex interface whenever possible.
- Monitor through **headphones**, not speakers, to avoid acoustic feedback.
- Verify plugin licensing and JUCE/ASIO GPLv3/AGPLv3 obligations before distribution.

## Acceptance test (not inferred from successful compilation)
- Windows CI green and packaged executable downloads.
- Native output stays silent before user enables monitoring.
- 48 kHz mic -> headphone dry passthrough works.
- Known stereo VST3 changes audible signal; bypass restores dry signal.
- Real device name/rate/buffer/plugin latency reported, meter responds only to real output.
- Startup/shutdown repeat 20x without crash, test 30 min voice monitoring.
- Test ASIO and WASAPI separately, log latency and glitches. This P1 implementation does **not** yet count x-runs.
