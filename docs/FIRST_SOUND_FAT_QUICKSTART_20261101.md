# ArSonMix — Windows First Sound / Hardware FAT
Date: 1 November 2026. M1 hardware candidate addendum: 2 November 2026. Current production: **10 input channels + 4 AUX**, one JUCE callback, MAIN/MONITOR and native VST3. The experimental dynamic CH11/Group engine has **not** replaced the production callback. The outstanding 0FX hardware crackle issue is #15. This is a FAT **candidate**, not hardware certified or guaranteed zero physical latency.

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

## 5. M1 candidate — structured physical acceptance (not an M5 universal graph)

The **ArSonMix-M1-EXPERIMENTAL-Hardware-FAT-Windows-x64** workflow artifact is uploaded only if the exact PR SHA completes Windows compile, all CTests, packaging and ZIP/embedded-GUI SHA256 verification. A PR artifact is not a public stable release. The EXE preserves the existing **native M4 10CH + 4AUX** callback, not a live CH11/group universal engine. DO NOT apply an unqualified artifact on a live public-performance show.

One real device, one verified I/O clock, wired monitoring, LOW output gain and initially **0 VST3**. Start at a supported buffer of 256 samples, then 128 and 64 only after the earlier stage has passed; document actual negotiated values, don't assume the driver honored the request. For each supported buffer:

| Native check | Operator action and expected physical result | Hard fail / retain evidence |
|---|---|---|
| Unpatched startup | Launch without input patch; MAIN stays silent and unarmed | Any unexpected output/audio or wrong endpoint |
| Device isolation | Select real active ASIO or WASAPI endpoint; verify L/R physical pins, rate and epoch | Invalid pins, auto re-arm on hotplug, unexpected fallback |
| Real 0FX | Patch CH01 mic/line input, explicitly ARM; record 30-minute continuous wired output | Pops/crackle: Mark Crackle and Copy Full Report |
| Short-buffer gain | Move CH01 fader repeatedly and reverse while moving; listen for clicks, watch true meters | Zipper clicks, freezes, inconsistent physical gain |
| AUX gain/edge | Connect a validated SEND to AUX; change send and AUX return levels with effects bypassed | Silence despite accepted change, stale/reverted controls, double direct signal |
| Monitor isolation | If a separate hardware pair exists, enable MONITOR intentionally | MONITOR leaking to MAIN or shared hardware pair |
| Vendor/plugin | Only after 0FX clean, insert real VST3 and toggle bypass/editor | Stuck UI, plugin recreation, audio gap, FX state loss |
| Show lifecycle | Save and recall same route/plugin states, check output is never armed implicitly | Stale device epoch, missing FX, corrupt restoration |
| Safe shutdown | Stop/restart and unplug/replug device, reselect/ARM deliberately | Audio on unexpected pins, leaked resource or crash |

**Required evidence**: download artifact name and exact 40-character SHA from `BUILD_PROVENANCE.json`; SHA256 of installed EXE; physical device name/driver/rate/buffer and enabled port mapping; 0FX duration; crackle report if any; one short video or screenshots demonstrating CH01, AUX, MONITOR and Show. The test passes only for the exact device and configuration tested, and never proves every VST3 or absolute crash-proof operation.
