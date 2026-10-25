# ArSonMix — Live Mixer & VST3 Host

**Project status (25 October 2026):** the premium **GUI v0.8** has been visually approved and merged into `main`. The browser preview remains a **UI prototype with simulated audio/meters**. The native Windows/JUCE/VST3 engine is progressing separately through stacked draft PRs; those builds and hardware paths are **not** certified by the GUI merge.

> **GUI SOURCE OF TRUTH — LOCKED:** [GUI v0.8 baseline](docs/GUI_BASELINE_LOCK_2026-10-25.md) · approved commit `343692fc9b702661a8455cca30f0ffd108907324` · merged in [PR #2](https://github.com/masarray/arsonmix/pull/2) · preserved as `checkpoint/gui-v0.8-approved-20261025`. **Do not restore the previous GUI from native-engine branches.**

ArSonMix is a Windows-first digital mixer and low-latency live VST3 host project for live vocals, karaoke and livestreaming. Its interface emphasizes fast **FX Rack / insert power**, tactile console controls, accurate channel identification and uncluttered mixing.

## Open the approved mixer

- **Landing:** https://masarray.github.io/arsonmix/
- **Interactive mixer:** https://masarray.github.io/arsonmix/preview/
- **Standalone full-screen GUI:** https://masarray.github.io/arsonmix/preview/mixer.html
- **Deployed commit:** https://masarray.github.io/arsonmix/preview/build.json

**Pages builds the canonical standalone GUI from `main`, after the main-branch Chromium smoke test succeeds.** The last visual sign-off is GUI v0.8; a later change must preserve the accepted design or obtain another explicit review.

## Approved GUI v0.8 features

- 12 color-coded input strips plus aligned stereo **MASTER strip #13**; full-height amber selected-strip highlight.
- 6 visible FX inserts per strip and master; independent compact on/off bypass for occupied inserts and separate open/edit action.
- Mirror-symmetric silver metal faders, distinct red master fader, relative no-jump pointer drag.
- Compact horizontal PAN with L/C/R values, restrained illuminated fill and noticeable center detent; master PAN included.
- Fixed hardware legend **MUTE** (red = muted); SOLO yellow, HPF green. Context-specific supplied metal button families, without global restyling.
- Stereo master L/R display with peak holds and clip latches; two streaming reference readouts only: **LUFS-I** and **dBTP**.
- Channel name/color/icon editor, quick-sound presets, mixer snapshots, explicit **SAVE SNAPSHOT / SAVE FX / EXPORT** actions.
- One application toolbar, organized left rail, and GitHub Pages preview with SHA-aware reload.

**Accuracy boundary:** The browser's LUFS-I, dBTP, peak and VU values are illustrative. They are **not ITU-R BS.1770 measurements** and do not prove audio loss, clipping or real-world latency. When integrating the native engine, use authentic DSP data and never mix fake demo metering into the live audio UI.

## Working on the project / handing off to another thread

**Start by reading [AGENTS.md](AGENTS.md) and [docs/GUI_BASELINE_LOCK_2026-10-25.md](docs/GUI_BASELINE_LOCK_2026-10-25.md).** The approved GUI is in `main`; do not use the historical GUI branch as the source for new designs. If working on native VST3, apply the engine to the new GUI contract rather than replacing `web/app.js`, `web/styles.css` or the standalone HTML with a stale copy.

The native PR chain [#1](https://github.com/masarray/arsonmix/pull/1), [#3–#10](https://github.com/masarray/arsonmix/pulls) and reconciliation PR [#11](https://github.com/masarray/arsonmix/pull/11) represent **separate in-progress engineering work**. Merge only after native build/test qualification, user hardware FAT and UI baseline parity; do not close these unfinished PRs just to tidy the issue list.

## Local preview and tests

Open `AR-MIX-Live-FX-Host-Standalone-v0.3.html` with Chrome/Edge for an offline GUI trial. The standalone's filename is historical; its content is the **approved v0.8 GUI**.

```bash
node --check web/app.js
python tests/button_asset_test.py
python tests/fader_asset_test.py
python tests/pan_button_asset_test.py
python tests/gui_baseline_lock_test.py
python tests/ui_smoke.py
python scripts/build_pages.py --out dist-pages --branch main
```

The Chromium smoke test needs Playwright Chromium installed. `.github/workflows/ui-smoke.yml` runs it for pull requests and relevant `main` changes. The single `.github/workflows/pages-publish.yml` publisher runs **after successful main GUI smoke**. See [Pages runbook](docs/GITHUB_PAGES_PREVIEW.md).

## Windows native host

`src/`, `engine/`, `CMakeLists.txt`, and `build_windows.ps1` in `main` are the original scaffold and reusable real-time primitives; evolving native versions are held in separate PR branches. Use Visual Studio 2022/CMake/JUCE and suitable ASIO/WASAPI drivers for native development. Browser-only UI acceptance is **not** proof of native VST3 load, FabFilter/RX compatibility, audio callback reliability, interop or full-fidelity plugin state.

See the historical [architecture & roadmap](docs/ARD_AR_MIX_LIVE_FX_HOST_v0.3.md) and [GUI checkpoint](docs/GUI_BASELINE_LOCK_2026-10-25.md) for current precedence.

## License and assets

The repository contains project code and user-supplied visual PNG artwork. Retain notices and verify redistribution permission for every external visual asset, VST3 binary, JUCE/ASIO/VST3 SDK and third-party dependency before public binary release. The GUI milestone is **not a native product release**.
