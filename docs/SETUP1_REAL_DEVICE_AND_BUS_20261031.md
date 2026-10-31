# SETUP1 — Native hardware device selection and truthful BUS patch

31 October 2026 · audited baseline `91115f3755998ede9261a4589599fc465cc44908`.

## Source-backed root causes

The main AUDIO DEVICE click was intercepted by web/mixer-io.js to open the INPUT ROUTING matrix rather than actual driver selection. The ASIO/WASAPI device chooser was behind the third tab and DRIVER SETTINGS. Worse, src/Main.cpp constructed JUCE `AudioDeviceSelectorComponent(manager,1,2,1,2,...)`, restricting enable-able hardware capture/playback pins to **2**; on multiport PS22 interfaces virtual channels above pin 2 and physical MONITOR 3/4 could be inaccessible even if the device exposed them.

## Delivered and scope

- AUDIO DEVICE now opens the device/driver tab first, with only JUCE-reported ASIO, Windows Audio/WASAPI and legacy DirectSound availability and capture/playback names. SELECT AUDIO DRIVER / DEVICE invokes the **existing native JUCE** device selector, not simulated JS selection; REFRESH triggers native inventory. Web preview states unavailable.
- JUCE native selector now permits up to **64** enabled physical input and output pins; this is a selector limit, not 64 live mixer channels or additional DSP. CMake explicit flags JUCE_ASIO=1, JUCE_WASAPI=1, JUCE_DIRECTSOUND=1. No endpoint silently selected or armed; DirectSound is higher-latency legacy compatibility only when actually enumerated.
- PATCH opens AUX / BUSSING and illustrates only implemented CH1–10 → PRE/POST AUX1–4 stereo FX RETURN → MAIN, with separate verified MONITOR output. Buttons navigate to native sends, FX returns and MAIN/MONITOR output config. Advanced source/channel controls remain collapsible. **GROUP, DCA, MATRIX and CUE are NOT IMPLEMENTED**, not interactive fake bus types.
- The real monitor.js and JUCE native one-session/one-graph routing epoch/revision safety, MUTE→APPLY→positive ACK, no auto-unmute, MAIN/MONITOR/OUTPUT1, VST3 and approved L2/RAIL3/fader layout remain.

## Acceptance / remaining FAT

Chromium native-event mock tests ASIO/WASAPI/DirectSound inventory, device-first entry, click opens actual JUCE setup action, then input routing CH3/CH6 staging remains, and bus patch navigation. Windows native CTest/build must confirm 64-pin selector flags. **Actual PS22 virtual capture pins** and non-adjacent L/R, ASIO/WASAPI input/output audibility, physical MONITOR on 3/4, driver selection, iGPU latency and zero-FX crackle issue #15 require physical FAT. A backend option in source or inventory is not a hardware qualification.

Canonical UX remains PATCH2 PRD/ARD and product truth; this milestone fixes the most serious discoverability/enablement defects, not live-safe quick patch or full LV1 group/matrix parity.
