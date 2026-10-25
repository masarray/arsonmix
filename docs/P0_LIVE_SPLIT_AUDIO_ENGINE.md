# P0 Live Smart Audio — independent Windows capture/render integration

**2026-10-25 · implementation review status:** Windows CI pending on PR branch; physical 44.1/48 kHz crackling FAT not run. Never mark P0 hardware acceptance GREEN from simulated test.

## What is actually different from PR #21

- **Two independently started JUCE Windows Audio devices** (separate capture `AudioDeviceManager` and render manager) replace the earlier single joined I/O callback in Smart mode. The producer capture callback deposits raw audio in preallocated SPSC FIFO; the render callback clocks the VST3 mixer through bounded polyphase FIR ASRC. There is **no extra user thread per channel**.
- Only the Windows Audio shared backend is eligible for the dual-stream route; **ASIO and exclusive** retain their existing direct fast path (no added FIFO, no additional SRC).
- **AUTO** is the default: tries split capture/render for two different selected Windows Audio endpoint names. Failure to open either endpoint is caught; stops capture, restores the original full-duplex JUCE setup, and records a diagnostic. Distinct names are only a conservative *candidate signal*, **not proof that two hardware clocks differ**.
- Profile menu in the native-only SIGNAL CHECK: AUTO (20 ms FIFO fill), STABLE (40 ms), LOW LATENCY (12 ms), DIRECT (no bridge). These are provisional **additional FIFO targets**, not total end-to-end latency; output device and wireless transport add their own latency. The selected profile persists locally.
- One-sided OUTPUT ONLY/CAPTURE ONLY tests remain accessible: Smart stream stops before diagnostic routing changes; after NORMAL the selected Smart profile is attempted again. Editing audio device configuration reverts to the existing supported device selector; temporary split or diagnostic setups **never overwrite the saved device XML**.
- A control-thread watchdog detects capture callback stoppage or >3 seconds with no capture callback; this fails over once to direct mode. It does not attempt repeated restart storms, does not restart on the audio thread, and preserves the old route on failed restoration.
- Existing native **COPY FULL REPORT** includes routing mode, capture and render rates, periods, FIFO fill, ratio correction ppm, host FIFO overrun/underflow/priming counts, capture backend xrun provenance, callback timing and timestamped markers. This does **not** imply render hardware underrun telemetry is available through generic JUCE.

## Realtime invariants

Audio callback is **always** either the old direct callback, or a render callback pulling from a prepared SPSC FIFO. Independent capture callback only does bounded copying + relaxed atomic telemetry. Device teardown/initialization, plugin preparation, diagnostics writer, Clipboard and WebView are control-thread operations. FIFO preparation happens after endpoints have been negotiated and before the callbacks are attached. A route transition mutes the output while detaching callbacks and restoring the device.

Cross-clock drift is corrected by the precomputed 48-tap/1024-phase FIR and bounded PI loop; 44.1→48, 48→44.1, +100/+300/−300 ppm deterministic CTests remain required. An additional 30 virtual-minute 8kHz test includes −300 ppm drift and short capture scheduling stalls. Do not infer CPU headroom for all processors/sample rates from this short test alone.

## Remaining non-negotiable P0 acceptance

1. Windows MSVC x64 Release compile, whole CTest suite, native/JS contract and Chromium smoke green on **exact commit**.
2. Realtime capture/render drop/race stability under real WASAPI. Neither JUCE backend output-underrun nor device hardware timestamps are currently exposed; **hardware render starvation remains unavailable**, not zero.
3. Device stop/unplug/replug, output sample-rate renegotiation, shared-period contention, 44.1 and 48 kHz + virtual cable / Bluetooth / wired interface all need physical FAT. Unknown clocks can still be unsupported; never claim zero glitches on every driver.
4. Proof-quality SRC tests (multi-tone/sweep, stop-band alias, impulse delay, CPU, 30-minute soak) and end-to-end sub-10ms targets remain **device-dependent**. 12/20/40 ms target FIFO profiles cannot honestly be described as universal ultra-low latency.
5. Third-party VST3 callback safety remains a separate P2.5.2 scope. This P0 update changes I/O, not add/remove graph APIs.

## User acceptance workflow (ONE campaign)

The user should not be forced to manually test after every code step. When actual Windows CI is green and a complete P0 candidate is packaged, test both 44.1 and 48 kHz across wired and reported virtual/BT endpoints. Listen at low volume for 0-VST3 dry / split / output-only, mark crackle, and click **COPY FULL REPORT** once to paste in the engineering conversation. Report backend names/modes, selected profiles, FIFO counters and any captured onset times. Only after measured real-device results should P0 hardware issue #15 be closed.

Primary references: Microsoft WASAPI IAudioClient3, IAudioCaptureClient GetBuffer flags (DATA_DISCONTINUITY), JUCE 8.0.15 AudioDeviceManager / AudioIODeviceCallback. P0 issue: https://github.com/masarray/arsonmix/issues/15
