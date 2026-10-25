# ArSonMix P2.2 — Investigating recurring audio dropouts

**Status:** Experimental Windows diagnostic build. Performance counters are evidence, not a concluded root cause.

## Current evidence from the user's PC
- VST3 host produces audio when output is enabled, thus at least one valid input→VST→master→Windows Audio output path exists.
- Displayed device: Windows Audio, 48 kHz, 480-sample buffer = 10 ms nominal buffer period.
- The VST3 chain advertised 960 samples processing latency = 20 ms at 48 kHz. This is plugin latency **not** measured system round-trip latency.
- Reported audio pauses every few seconds. Source inspection found an old 3-second synchronous device XML state poll on the message thread. That was unnecessary and is now removed, but it is a candidate cause, **not a proven diagnosis**.
- Other candidates include WASAPI scheduling irregularity, slow VST3 processBlock, input/output endpoint contention, and device stop/restart events.

## Implemented changes
1. **Auto-routing:** with a valid duplex device selected, the output is active without an extra arm button. A user-accessible emergency MUTE OUTPUT in SIGNAL CHECK remains available; use headphones at low volume to prevent feedback.
2. **Event-driven settings:** AudioDeviceManager ChangeListener marks device XML dirty. It is no longer repeatedly serialized and written on a 36-tick / 3-second timer.
3. **Realtime measurements:** atomic counters are updated inside the audio callback, with no file I/O, locks, heap allocations in host callback logic, or GUI calls. Measures callback duration, worst duration, processing beyond 80% of available callback period, over-budget callbacks, long callback scheduling gaps and slow plugin time.
4. **Background diagnostics:** a bounded 128-record SPSC UI→worker queue writes 1 Hz health snapshots and event markers to a rotating 2 MB log (plus previous). The plugin/audio callback only publishes numeric counters, not file records.
5. **Visual diagnostics:** SIGNAL CHECK shows live input/post-FX/output meters plus deadline timing; OPEN DIAGNOSTIC LOG reveals the local file.
6. **Current GUI:** imported current source (ArSonMix brand, metallic faders, horizontal PAN slider, tactile buttons) from GUI branch `fix/fader-thumb-symmetry-polish`. Maintains working native VST3 plugin picker/catalog.
7. **Live FX structural operations:** add/remove/reorder may still briefly detach callbacks. Do not perform them during a performance until graph hot swap is engineered.

## Reproduction / engineering triage

1. Choose the same selected input and headphone output. Start with 48 kHz / 480 samples to reproduce reported problem, keep output/headphone volume low.
2. In SIGNAL CHECK, note worst callback duration, over-budget count, callback-gap count and plugin time. Keep recording for at least 2–3 minutes through the intermittent symptom.
3. First test with **DRY TEST ON** (all VST3 skipped, same audio device). Observe and record if dropout remains. Then turn DRY TEST OFF and test each plugin in isolation.
4. Try ASIO on one full-duplex interface, initially 128–256 samples if supported; separately compare Windows Audio/WASAPI. Driver and endpoint selection must be changed through Audio Setup.
5. Use OPEN DIAGNOSTIC LOG. Copy the latest log around the timestamps of audible stutters, with device and plugin names sanitized if sharing publicly.
6. Interpret cautiously:
   - Slow callback/over-budget jumps near a dropout suggest a DSP deadline miss (plugin processing or callback).
   - Slow plugin increments suggest one or more VST3 calls are consuming most of the deadline. This release logs each VST3 slot's last and worst processing time to help identify a slow insert. Compare slot timings and use DRY TEST to confirm causality.
   - A callback scheduling gap jump with low process time suggests driver/OS scheduling or device discontinuity.
   - Flat counters despite audible loss suggest an endpoint, physical device, external virtual cable, or a discontinuity not captured by this timing probe.
7. Success criterion: sustained uninterrupted mic→dry→master output and then mic→VST3→master output; no observed dropout on the actual physical interface.

## Engineering limits / follow-up
- These are **timing indicators, not driver-confirmed xrun counts**. Device-specific ASIO/WASAPI dropout callback telemetry is still missing.
- Third-party VST3 plugin code may allocate or block; the host cannot enforce its RT safety.
- No complete plugin crash sandbox or seamless FX graph swap yet.
- Real output to streaming apps is separate from the output endpoint in the JUCE device selector. UI “Record/Streaming” output buttons remain previews until real routing is implemented.
- The log may contain local device/plugin names; treat it as diagnostic user data.

## Additional telemetry
Audio-device start and stop callback counts are recorded, together with per-VST3 slot last/worst process duration (microseconds). A rise in device stop/start around stutters indicates a device lifecycle event; a rise in one slot's processing duration indicates a potentially expensive effect. Time counters alone do not prove that a device underrun occurred.
