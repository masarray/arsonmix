# P0-00 — Audio Endpoint Isolation: engineering test runbook

**Status:** Experimental native diagnostic build; NOT a crackling fix or audio certification  
**Reference:** [Smart Audio Engine ARD v1.0](ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md), [P0 #15](https://github.com/masarray/arsonmix/issues/15)  
**UI authority:** Approved GUI v0.8. This feature adds controls only inside existing native-only SIGNAL CHECK overlay, without changing the mixer visual layout.

## Purpose

Isolate which part of Windows audio is interrupted **before VST3 is involved**, without inventing xrun measurements or assuming an ASIO/BT/Virtual Cable driver is at fault. The modes are real endpoint configurations, not muting a meter:

| Mode | Capture endpoint | Render endpoint | Audio played | FX |
|---|---|---|---|---|
| NORMAL | Selected user input | Selected user output | Normal Channel 1 mixer | As user selected |
| DRY THROUGH (0 FX) | Selected user input | Selected user output | Direct dry path (fader, PAN, mute remain) | Entire VST3 graph bypassed |
| OUTPUT ONLY | **closed** | Selected user output | Native 440 Hz sine, nominal **-30 dBFS**, 8 ms amplitude ramp | No FX processing |
| CAPTURE ONLY | Selected user input | **closed** | **Nothing**; meters and input/driver stats only | No FX processing |

The one-endpoint modes call JUCE `AudioDeviceManager::setAudioDeviceSetup` with the unused endpoint removed. Before changing modes, the device callback is stopped/restarted by JUCE; that is an **intentional controlled test interruption**, not the adaptive seamless device engine planned for P0-01/P0-04.

**Safety:** lower physical headphone/speaker volume before selecting OUTPUT ONLY. Tone amplitude is limited in host output, but an external amplifier can still be loud. Use the native master output mute for emergency silence.

## How to collect comparable evidence

1. Run the Windows EXE from the exact P0-00 CI SHA; open AUDIO SETUP and select the two endpoint names used in the earlier crackling report. Start with 48 kHz and the system-supported buffer; do not blindly reduce buffer.
2. Open **SIGNAL CHECK** and find **P0 AUDIO ISOLATION**.
3. Switch to **OUTPUT ONLY**. Capture is truly closed. Listen for clear 440 Hz for at least 60 s; click **MARK CRACKLE** at any audible defect. Record whether it persists if only the output is changed to a wired device.
4. Choose **CAPTURE ONLY**. Hardware output is closed; this test must not be audible. Watch input meter and backend capture discontinuity field while the same virtual input runs. Mark observed capture problems, if any.
5. Choose **DRY THROUGH (0 FX)**. Confirm that actual microphone/PCM routes to output, while third-party plugin process blocks are bypassed. Mark each crackle.
6. Repeat each mode at 44.1 and 48 kHz with the **same physical signal and level**. Only make rate/driver changes after returning to NORMAL and opening AUDIO SETUP.
7. Return to **NORMAL** before plugins/presets or closing. The original setup is restored automatically on leaving a one-endpoint mode. Temporary one-endpoint setups are not persisted in `audio-devices.xml`.
8. Use **OPEN DIAGNOSTIC LOG** and provide `audio-diagnostics.log` for comparison. The app writes `AUDIO_TEST_MODE` and `AUDIO_GLITCH_MARK` records with a snapshot of mode, device names, rate, buffer and native counters. No logging happens on the realtime thread.

## What telemetry can and cannot prove

- `lateCallbackIntervals`: callback start gap >1.5× nominal period; **scheduler indicator, not a confirmed xrun**. `callbackGapCount` remains the legacy >2.3× threshold for comparison.
- `backendXrunCount`: from `juce::AudioIODevice::getXRunCount()`; null when unavailable. On pinned JUCE 8.0.15 **WASAPI**, this represents reported input **DATA_DISCONTINUITY** flags, **not all capture FIFO shortfalls or output underruns**. Other backends expose driver-defined semantics.
- `renderUnderrunSource = UNAVAILABLE_IN_JUCE_8_WASAPI`: render starvation is **not directly observed** by the generic current callback. A zero in any timing counter is not proof that output playback is clean.
- `deviceGeneration` = number of callbacks started; `testOutputFrames` / `testCaptureFrames` = host diagnostic frames processed, not guarantee that endpoint delivered/consumed all of them.
- `callbackDurationUs`, `worstCallbackUs`, plugin counts and peaks remain host-processing estimates and snapshots, never total physical output latency.
- MARK CRACKLE correlates the person's listening observation with sampled counters; cannot automatically measure an actual hardware dropout.

## Decision matrix (hypotheses, not guarantees)

| Output-only tone | Capture-only driver | Dry-through | Next investigation |
|---|---|---|---|
| Crackles | May be clean | May crackle | Render endpoint/Windows scheduler/BT profile/driver |
| Clean | Capture discontinuities rise | Crackles | Capture path, different clocks, joined-WASAPI reservoir |
| Clean | Clean by available counters | Crackles | Cross-device timing; instrument deeper input reservoir & true output render starvation |
| Clean on wired, crackles on BT output | Variable | Variable | Output-specific buffering and Bluetooth transport |
| Clean across hardware | Clean | Clean | Compare against plugin graph structural edits; not proof of complete engine reliability |

## P0-00 definition of done

- Windows MSVC executable and diagnostic tone CTest PASS at 44.1/48/96 kHz virtual rates, and Chromium GUI v0.8 smoke unchanged.
- Real output-only mode opens zero capture channels, capture-only mode opens zero render channels, dry path skips all actual VST3 `processBlock`. Device setup restores without persisting temporary selection.
- Annotated 48k/44.1k hardware evidence collected; xrun source correctly labeled; no false claim of real render underrun detection.
- **Hardware listening acceptance is separate:** P0-00 does not solve drift, render starvation or flaky drivers. Clock-domain instrumentation and correction come only after P0-01/P0-02 feasibility.

**Pending known engine limitations (unchanged in this slice):** no independent capture/render callbacks, no adaptive SRC, no real output underrun counter through generic JUCE, FX insert/remove/move still detach the callback, Channel 2–12 non-native.
