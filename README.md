# ArSonMix

**Open-source live VST3 host with a premium v0.8 virtual digital mixer interface.** Built for Windows live vocal, karaoke and streaming workflows, with a JUCE C++20 audio engine and a WebView2 console UI.

[Premium browser GUI preview](site/preview/index.html) · [Experimental Windows EXE builds](https://github.com/masarray/arsonmix/actions/workflows/p1-windows.yml?query=branch%3Aintegration%2Fp25-gui-engine-reconciled) · [Progress & release status](docs/PROGRESS_RECONCILIATION_2026-10-25.md) · [ARD](docs/ARD_AR_MIX_LIVE_FX_HOST_v0.3.md)

## Current development baseline · 25 October 2026

| Area | Working development scope | Important limit |
|---|---|---|
| GUI & UX | Latest tactile/metallic v0.5 PC console design, channel identity editor (name/icon/color), PAN snapping, red Master fader, peak-hold & clip LEDs | Browser-only UI controls are not proof of audio DSP functionality |
| Audio | JUCE native callback, Windows ASIO/WASAPI device selection, auto output routing, real meters, PAN, channel/master faders and MUTE | Real processing currently uses **Channel 1 stereo**; input strips 2–12 are previews |
| VST3 | Scan/import/catalog, insert/editor, atomic bypass and diagnostic messages | FabFilter and iZotope RX were confirmed loading on prior P2.4.1 hardware; retest integrated build |
| Presets | Native `.armix` ZIP + vendor binary plugin state, FX order/bypass, Channel 1/master gain and PAN | Save requires muted output; presets depend on locally installed VST3 files |
| Live recall | Experimental P2.5 prepared graph, callback-boundary de-click fade, deferred plugin retirement | Not seamless dual-graph crossfade. FX ADD/REMOVE/REORDER still detach callback |
| Master | Real Master level/MUTE plus independent L/R meters | Master FX inserts, master PAN, LUFS-I and dBTP **not native**; visual previews/placeholder fields |
| Routing | Functional Channel 1 to selected hardware output | No true independent 12-channel, streaming/record buses or automatic loopback setup yet |

The canonical reconciliation source is **`integration/main-v08-gui-p25-native`**, preserving both the best [GUI branch](https://github.com/masarray/arsonmix/tree/main) and the advanced [P2.5 engine branch](https://github.com/masarray/arsonmix/tree/feature/p25-live-preset-graph-swap). Current `main` already contains approved GUI v0.8 (PR #2). This integration layers the native P2.5 engine on that exact v0.8 base without rolling back visual changes. Previous engineering PRs remain preserved until post-merge verification.

## Try it

**Visual preview:** open `AR-MIX-Live-FX-Host-Standalone-v0.3.html` directly in a current browser, or use the `site/preview` UI. Browser preview uses animated demonstration meters and dummy FX. It cannot process your microphone.

**Actual Windows audio:** from the linked Windows Actions workflow, open the latest **successful integration-branch run** and download its EXE artifact. Use wired headphones at a low level; configure ASIO/WASAPI; open `MIX → ADD FX` to scan/select your own installed VST3, then edit and bypass it in the rack. The source does **not** redistribute FabFilter, iZotope, or other proprietary plugins.

For native sound presets, create a sound, mute output and choose `MY SOUNDS → SAVE`. Re-enable output before using experimental `RECALL LIVE`. Check `SIGNAL CHECK` and `audio-diagnostics.log` after a test. Avoid changing audio devices during graph transitions.

This is an **engineering preview**, not qualified for an unattended livestream. Do not equate passing CI with verified dropout-free ASIO/WASAPI or all-plugin compatibility.

## Building the Windows native app

Requires Visual Studio 2022 C++, CMake 3.22+, compatible JUCE 8, WebView2 and the appropriate ASIO SDK/licensing components.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_ROOT="C:/dev/JUCE"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The source tree separates `src/` (JUCE native engine, VST3 host, diagnostics and state), `web/` (premium console and native bridge), `engine/core/` (reusable realtime controls), `tests/` (native and visual regressions), `docs/` (ARD and phase guides) and `site/` (browser-only landing/preview). GitHub CI tests both Windows MSVC and Chromium layout independently.

## Planned progression

1. **P2.5.2** — safe live ADD/REMOVE/REORDER, preparation worker, plugin/device lifecycle and measured crossfade/latency tradeoffs.
2. **P2.6** — actual multichannel input mixer, per-channel VST racks, routing and independent output buses.
3. **P2.7** — complete native session/snapshot persistence, autosave recovery, sound library and beginner-focused flow.
4. Hardware validation and stability gates with common VST3 plugins, including FabFilter/RX, before promoting a qualified release.

For the lineage of all parallel threads and outstanding tests, read [the reconciliation ledger](docs/PROGRESS_RECONCILIATION_2026-10-25.md). Previous README content remains preserved in [historical notes](docs/HISTORICAL_README_PRE_RECONCILIATION.md).

## License

Project code is published under AGPL-3.0-only. JUCE, ASIO, VST3 SDK, third-party artwork and plugin licenses are independent and must be reviewed before redistribution; commercial VST binaries are not included.
