# ArSonMix — Windows First Sound / Hardware FAT
Date: 1 November 2026. Current production: **10 input channels + 4 AUX**, one JUCE callback, MAIN/MONITOR and native VST3. The experimental dynamic CH11/Group engine has **not** replaced the production callback. The outstanding 0FX hardware crackle issue is #15. This is a FAT **candidate**, not hardware certified or guaranteed zero physical latency.

## 1. Verify package
Extract the complete Windows Actions ZIP to a writable folder. `BUILD_PROVENANCE.json` must show the same `sourceCommit` as the GitHub run; EXE SHA-256 must equal `exeSha256`. Keep `portable_mode.txt`, never delete an existing `Studio` workspace. The file is a Windows x64 JUCE GUI executable; Microsoft Edge WebView2 Runtime and the correct audio hardware driver are required.

## 2. One user APPLY → first audible sound
1. Plug in wired headphones/amplifier at **LOW volume**. Start with **0 VST3**. Disable direct-monitoring on the audio interface if it would obscure testing the software fader/mute path. Output begins MUTED by design.
2. Open **SETUP / AUDIO I/O → DEVICE / DRIVER**. Choose one verified same-interface ASIO capture/playback device when possible, or the Windows Audio device you intend to use. Confirm actual driver, buffer, sample rate, enabled input pins and output L/R pins. **APPLY DEVICE** and wait for native accepted readback; selecting a DRAFT device is not enough. Changing an endpoint never auto-arms output.
3. In **INPUT ROUTING**, select an actual active mono pin for CH01 (optionally a second distinct input for CH02), then **USE INPUT**. The console **APPLY** command owns a guarded SAFE MUTE → native patch ACK → deliberate ARM workflow when eligible. Check accepted routing revision **and** OUTPUT ARMED. The UI has no separate manual START AUDIO prerequisite.
4. Inspect **RAW PCM NOW** for the true capture pin; check the corresponding CH meter and MAIN L/R. At safe listening volume, verify CH01 fader and CH01 MUTE change **physical** output and that MASTER MUTE silences it. Native route ACK alone is not proof of incoming audio or audible MAIN.
5. On a clean 0FX path, add a **real installed VST3** to one routed channel, then check bypass, editor and audible effect. Do not count a browser demo FX slot as a loaded plugin.

**Source routing:** ArSonMix does not force OBS or VB-Cable. For VB-Cable, the source application must play into **CABLE Input** for ArSonMix to capture **CABLE Output**. Virtual Maono VC pins are not necessarily hardware microphone pins; use the verified device’s reported mapping.

## 3. Reproduce or isolate any crackle (#15)
With a driver-supported, moderate buffer first, compare actual negotiated **48,000 Hz** and **44,100 Hz** if both are supported:
- **OUTPUT_ONLY_TONE:** playback without capture (suspected render path if crackle persists).
- **CAPTURE_ONLY:** input callback observation with muted output (checks real pin signal and available capture flags).
- **0FX normal:** one real input → CH01 fader → MAIN wired headphones, test continuously for **at least 30 minutes**.
- **1FX / multiFX:** only after 0FX clean; record plugin names and latency. Lower buffer sizes can increase dropout risk.

If crackling occurs use **SIGNAL CHECK → MARK CRACKLE → COPY FULL REPORT** and include its raw text. Capture discontinuity flags and late callback timing are not, by themselves, proof of a DAC/render underrun. Any device change, hotplug or secondary-stream clock switch must leave output muted until explicit re-ARM. Never feed an unknown speaker by default.

## 4. Return evidence / acceptance
Send: (a) screenshot of active driver/rate/buffer/pins, (b) accepted CH01 patch and OUTPUT ARMED, (c) result of physically moving CH01 fader/MUTE and MASTER MUTE, (d) audible 0FX duration and any crackles, (e) Copy Full Report including markers when a glitch occurs. Only then proceed to VST3 and FX/send/Show checks.

**PASS is hardware-grounded:** correct MAIN source path, verified mute/fader control, no wrong-endpoint output, clean 0FX wired audio for >=30 minutes on the recorded device/rate/buffer, followed by real plugin testing. A green Windows CTest/Chromium CI is not a substitute.

Historical full test matrix remains `docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md`. This packaged guide supersedes obsolete manual START AUDIO instructions.
