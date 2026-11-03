# M5.1 — Joined latest GUI #271 and verified ENGINE #266: hardware FAT

This exact commit joins **ENGINE** `9d0ecabbdd3ba2b1629a8c3917ee64e0b6ae139d` (native MSVC/CTest/Windows packaging PASS, run 41550962882) and **GUI** `67ef6e7b0afab173840e611b7fdf6627a96e714a` (same original UI files, no unauthorized GUI-branch rewrite).

## Real integration vertical slice

`src/Main.cpp` retains its ONE physical JUCE callback (native M4 10CH+4 AUX and vendor plugin ownership). `src/ConsoleLiveBridgeV6.h` publishes the separately namespaced, route/epoch/fault-fenced `legacyM4` accepted 10-channel roster and supported operations without inventing permanent ChannelIds or claiming full M5 cutover.

`web/m4-native-console.js` listens on the same existing `arMixConsoleSnapshot/Intent/Ack` WebView2 socket, checks exact canonical decimal revisions and real physical port roster, and renders explicitly marked **M4 NATIVE** controls only when the native accepted physical route is healthy. Fader, PAN, MUTE/SOLO, mono physical source and accepted vendor VST3 bypass/editor commands go to the single native C++ executor; no browser DSP, duplicate callback or plugin instance. Every gesture is coalesced to one transaction per channel and locked pending ACK + *subsequent matching native readback*. Structural changes/hotplug/failed authority disarm the view; the original native advanced routing matrix remains reachable. Universal V6 `productionCallbackLive=false` and `graph.channels=[]` remain truthful.

## Gates / scope

`tests/m4_native_gui_bridge_smoke.py` uses simulated native transport to validate actual intent payload fences, ACK-only pending, subsequent native fader readback, rejected MUTE and fail-closed authority loss. It is part of the joined Chromium workflow, not an acoustic sound certification. Existing Windows exact-SHA native CTest, packaging/provenance and Chromium gates must all pass before labeling this branch a distributable FAT candidate. Existing physical #15 crackle remains open; run actual 0FX wired 30-minute soak, VST3, independent source routing and Show FAT after using the verified ZIP.

**Remaining:** universal ChannelId single callback cutover with vendor CH1/Orbit/editor and full Show parity, richer GROUP/FX Return GUI and hardware FAT acoustic evidence. The M4 compatibility view must not be mistaken for that release milestone.
