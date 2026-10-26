# P0-14 — Actual release artifact and native engine/workflow audit (2026-10-27)

## User-reported failure: 648 KB instead of real application

Confirmed by direct container ZIP/PE inspection:

| Source | GitHub artifact | ZIP bytes | EXE bytes | PE subsystem | Truth |
|---|---:|---:|---:|---|---|
| Old `main@2ad3a673` (pre #74) | 12660174853 | ~663,586 | 1,378,304 | 3 = console | **Wrong ASIO Backend Registration Test**, must not distribute |
| PR #74 (prior corrected) | 12689000396 | 4,065,369 | 8,991,232 | 2 = GUI | Correct JUCE native app, PR synthetic checkout SHA |
| `main@1dc06395041713c42b2161b0cd1c0a2b19ec8996` | 12689526022 | **4,065,367** | **8,991,232** | **2 = GUI** | Canonical main source SHA, verified |

Both correct packages contain exactly an EXE, README.md, BUILD_PROVENANCE.json, and HARDWARE_FAT_GUIDE.md. The main EXE SHA256 was `55b66c1a1d540f08f485f9c018cc469be5de45481e8d0235149439ea584cefee`. Direct ZIP extraction, CRC, PE machine AMD64=0x8664, PE32+=0x20b, subsystem=2, and manifest checksum all passed.

**IMPORTANT:** ZIP compression ratios vary. Small *compressed* size by itself is not proof of an invalid program, but in this repo the old 648KB package was definitively the **wrong target**. The new gates check a realistic minimum in addition to executable identity and embedded runtime assets. Thresholds are guardrails for this release branch, not a universally valid executable-size rule.

## Engine audit: what actually ships

- `CMakeLists.txt` uses `juce_add_gui_app(ARMixConsole PRODUCT_NAME "AR MIX Studio" NEEDS_WEBVIEW2 TRUE)` and `juce_add_binary_data(ARMixAssets SOURCES web/index.html web/styles.css web/app.js web/bridge.js web/monitor.js)`, linked to the GUI executable.
- `src/Main.cpp::assetFor` serves the five **compiled** resources via `BinaryData` to JUCE `WebBrowserComponent::Options::withResourceProvider`, and selects Windows WebView2. The correct main EXE was directly found to contain `function openChannelIdentity`, `class="master-analysis"`, `--ari-master-cap` (the latest rose/silver MASTER), `function syncNativeMixerControls` and `AUDIO_GLITCH_WINDOW` (P0-12).
- This is the latest user-approved **10-source-channel GUI PR #73 + MASTER**, merged and published on main. However, the experimental native JUCE DSP graph still implements **Channel 1 only**. A correct real-hardware EXE must not be represented as a qualified 10-channel digital audio mixer. No 0-FX hardware FAT has passed (#15 remains open).
- Windows WebView2 Runtime is provided by the OS/system, not bundled with the EXE. On a Windows 10 machine lacking it, the GUI may not load. Check the official Microsoft WebView2 Runtime installation, not the Visual Studio SDK NuGet CI download. JUCE source: https://juce.com/blog/juce-8-feature-overview-webview-uis/ and https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_gui_extra/misc/juce_WebBrowserComponent.h .
- There is no reliable render DAC hardware underrun count exposed by the pinned JUCE abstraction; callback gaps, WASAPI capture discontinuity and ASRC input underflows are different observations. Hardware audio must be tested. Do not claim that CI validates zero-FX audio quality, driver latency, or physical compatibility.

## New CI enforcement in P0-14

- **Fail-closed package stage** in `scripts/package_native_windows.ps1`: reject EXE below 4MiB while retaining exact GUI target path, PE subsystem and cryptographic provenance checks.
- **Independent distribution audit** in `scripts/verify_native_windows_artifact.ps1` on the same Windows CI runner, immediately before the artifact uploader: assert exact four distributable files, correct EXE size, PE x64 GUI subsystem, matching executable hash and `git rev-parse HEAD`, matching live source JS/CSS hashes, and **compiled** GUI/bridge/engine markers, not just source files.
- Compress release `dist/*` to a temporary ZIP, enforce at least 2MiB for this version and compare ZIP content entries including SHA256 of its actual decompressed executable. Remove test ZIP after audit; the upload step continues to use only the verified `dist/` folder. No extra Windows CI runner, no new workflow, no DSP callback modifications.
- Existing `tests/native_artifact_packaging_contract_test.mjs` extended to enforce that the actual audit stage remains before `actions/upload-artifact`.
- This closes the **release packaging regression class**, not the hardware-engine P0 #15. Hardware tests use the canonical verified main artifact; any reported 648KB ZIP should be discarded and its filename/run-ID checked before testing.

## Regression gate

Windows MSVC x64 Release, Node, CTest and Chromium green on the exact PR head; automated review resolved; independently inspected archive. Never merge solely on a green CMake build. Check both the latest main source SHA and the output artifact identity.
