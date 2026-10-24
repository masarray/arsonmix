# AR MIX Studio — Open-Source Live FX Host (v0.3 UI & Architecture)

**Status: P1 experimental native Windows first-sound implementation (pending CI/hardware verification) + v0.3 UI demo.**

AR MIX Studio targets live vocal processing for karaoke performers and livestream creators. It is VST3-first: the principal activity is building a beautiful, low-friction FX chain with a digital-console look. The core engine is planned as C++20/JUCE with Windows ASIO/WASAPI; the frontend is a self-contained HTML/CSS/SVG UI embedded in JUCE WebView2.

## Try the current prototype now

Open `AR-MIX-Live-FX-Host-Standalone-v0.3.html` in a current Chrome or Edge browser. No installation or internet access is required. The meters are intentionally **simulated**. Clicking `FX RACK` opens a chain editor: add a *demo* plugin, bypass/reorder/remove it; `MIX` offers faders, pan and MUTE/SOLO; `SIGNAL FLOW` describes the path; `SETUP` previews the driver wizard; `EASY VIEW` toggles Pro controls. Save a local FX chain preset or Mixer Snapshot, and export/import a JSON session file.

**Important:** catalog entries are visual demo effects, not installed VST3 binaries. Nothing will process live audio or activate your mic in this version. The Windows JUCE shell is source-only and has not been compiled/validated on Windows in this environment.

## Build JUCE WebView2 shell (Windows)

Prerequisites: Visual Studio 2022 with C++, CMake >=3.22, a local compatible JUCE 8/9 source checkout, and WebView2 runtime. See licenses before distribution.

```powershell
.\build_windows.ps1 -JuceRoot C:\dev\JUCE
```

CMake example:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_ROOT="C:/dev/JUCE"
cmake --build build --config Release
```

The shell embeds the same UI through JUCE BinaryData and uses `WebBrowserComponent::withResourceProvider`. `withEventListener("arMixParameter")` only logs events on the **JUCE message thread**. Implement the typed control dispatcher + RT backend before making it process audio. `JUCE_ASIO=1` is **not yet enabled** in this shell; check GPLv3 ASIO SDK compatibility and driver setup as specified in the ARD.

## C++20 realtime primitives test

```bash
g++ -std=c++20 -O2 -Wall -Wextra -pedantic -pthread tests/realtime_control_test.cpp -o rt_tests
./rt_tests
```

## Files

- `docs/ARD_AR_MIX_LIVE_FX_HOST_v0.3.md` — full product/technical architecture, RT rules, UX flows, benchmarks, roadmap and risk register.
- `docs/UI_AUDIT_AND_QA_v0.3.md` — source UI audit, fixes and executed smoke test.
- `web/{index.html,styles.css,app.js}` — maintainable UI frontend.
- `AR-MIX-Live-FX-Host-Standalone-v0.3.html` — complete single-file offline trial.
- `src/Main.cpp`, `CMakeLists.txt`, `build_windows.ps1` — native UI host scaffold.
- `engine/core/RealtimeControl.h`, `tests/realtime_control_test.cpp` — tested sample real-time control patterns, not complete DSP.

## Licensing

Code in this trial is authored for the project and provided under AGPL-3.0-only to align with JUCE open-source licensing. JUCE, ASIO SDK and VST3 SDK have their own notices and obligations; check exact version and dependency licensing before publishing binaries. No third-party VST binaries or proprietary mixer UI graphics are included.

## P1 Windows native First Sound

The new P1 native EXE provides actual microphone passthrough through one VST3 effect, ASIO/WASAPI device selector, bypass, monitor arm/mute, master gain and real callback meter. The old FX rack and channels 2–12 are **still UI previews** (do not confuse demo plugin inserts with native VST3). Output starts muted for hearing/feedback safety. See [P1 trial instructions](docs/P1_FIRST_SOUND.md).

GitHub Actions: [Windows First Sound builds](https://github.com/masarray/arsonmix/actions/workflows/p1-windows.yml). Download the artifact only after the run is GREEN. JUCE 8.0.15 is pinned by CI and includes GPLv3 ASIO SDK sources; this repository uses AGPLv3. Plugin compatibility and low latency must still be verified with real hardware.
