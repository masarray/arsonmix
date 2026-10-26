# ArSonMix — Live Mixer & VST3 Host

**Theme architecture handoff (design/reference only):** [Start-here for a new engineer](docs/THEME_ENGINE_NEW_THREAD_HANDOFF_2026-10-26.md) · [exact preserved Brushed Metal v1.0 CSS](design-references/brushed-metal-v1.0/adapter.css) · [reference reconstruction instructions](design-references/brushed-metal-v1.0/README.md). The theme engine is not yet built; no changes to production GUI/runtime are implied by these reference artifacts.

**Project status (25 October 2026):** premium **GUI v0.8** is approved and locked in `main`. The P2.5/P2.5.1 JUCE Windows/VST3 code was integrated through PRs #13/#14 as an **experimental native-development baseline**. Hardware tests reported **severe crackling with zero VST3**, so native audio has FAILED its P0 hardware acceptance; this is not a stable or broadcast-ready release. The browser preview remains visual-only, with intentionally simulated meters.

> **GUI SOURCE OF TRUTH — LOCKED:** [GUI v0.8 baseline](docs/GUI_BASELINE_LOCK_2026-10-25.md) · approved commit `343692fc9b702661a8455cca30f0ffd108907324` · merged in [PR #2](https://github.com/masarray/arsonmix/pull/2) · preserved as `checkpoint/gui-v0.8-approved-20261025`. **Do not restore the previous GUI from native-engine branches.**

ArSonMix is a Windows-first digital mixer and low-latency live VST3 host project for live vocals, karaoke and livestreaming. Its interface emphasizes fast **FX Rack / insert power**, tactile console controls, accurate channel identification and uncluttered mixing.

## Open the approved mixer

- **Landing:** https://masarray.github.io/arsonmix/
- **Interactive mixer:** https://masarray.github.io/arsonmix/preview/
- **Standalone full-screen GUI:** https://masarray.github.io/arsonmix/preview/mixer.html
- **Deployed commit:** https://masarray.github.io/arsonmix/preview/build.json

**Pages builds the canonical standalone GUI from `main`, after the main-branch Chromium smoke test succeeds.** The last visual sign-off is GUI v0.8; a later change must preserve the accepted design or obtain another explicit review.

## Proposed Multi-Theme Architecture (design checkpoint; not yet implemented)

A layout-safe Appearance system is planned to let users switch between **Studio Default** (approved GitHub appearance) and **Brushed Metal** (silver hardware, black-blue glossy FX glass), while the canonical GUI remains the sole owner of strip geometry and interactions.

- [Theme Engine ARD v1.0](docs/ARD_MULTITHEME_ENGINE_V1_2026-10-26.md)
- [Implementation PR ladder and verification strategy](docs/THEME_ENGINE_IMPLEMENTATION_PLAN_2026-10-26.md)
- [Tracking epic #44](https://github.com/masarray/arsonmix/issues/44)

**Status:** docs/ARD only. Neither runtime theme selector nor token engine exists in `main` yet. Work proceeds by individually validated T0–T7 PRs; current GUI lock, audio/native engine and Pages publishing stay unchanged.

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

Native changes from the stacked PRs [#1](https://github.com/masarray/arsonmix/pull/1) and [#3–#10](https://github.com/masarray/arsonmix/pulls) are now incorporated into `main` through the reconciled integration [PR #13](https://github.com/masarray/arsonmix/pull/13). The earlier PRs remain as historical/review references; their code integration does **not** certify third-party VST3 stability or real-device audio. PR #11 contains older v0.5 visuals and must never override approved v0.8.

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

`main` contains experimental JUCE C++20 ASIO/WASAPI **Channel 1** realtime audio, VST3 scanning/import/catalog, editor, FX bypass, FabFilter/RX-compatible layout, diagnostics, native `.armix` preset saving and P2.5 prepared-graph recall. It preserves GUI v0.8, while Channels 2–12 and Master FX/PAN remain non-native previews. The display does **not** claim measured BS.1770 LUFS-I or dBTP. Live recall uses a de-click fade (not seamless DSP crossfade), and ADD/REMOVE/REORDER still detach callback. FabFilter, iZotope RX, and other third-party plugins require Windows hardware FAT before a stable release. [Experimental Windows CI builds](https://github.com/masarray/arsonmix/actions/workflows/p1-windows.yml) provide artifacts from passing runs.

**P0 field support:** SIGNAL CHECK → **COPY FULL REPORT** copies the current native device, sample-rate/buffer, performance, source-labeled discontinuities and recent MARK CRACKLE events into the Windows clipboard for direct paste into the engineering thread. Experimental FIR/dual-clock code is a CI-tested core, not yet wired to physical WASAPI. [Detailed status and remaining gates](docs/P0_SMART_AUDIO_DIAGNOSTIC_AND_BRIDGE.md).

**Current native architecture (read first):** [Smart Audio Engine ARD v1.0](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md) · [implementation blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md) · [zero-FX crackling P0 issue #15](https://github.com/masarray/arsonmix/issues/15). The [older architecture v0.3](docs/ARD_AR_MIX_LIVE_FX_HOST_v0.3.md) is historical and superseded where inconsistent. [GUI v0.8](docs/GUI_BASELINE_LOCK_2026-10-25.md) remains locked. No P2.6 expansion until 44.1/48 kHz dry audio FAT is clean; no public-live claims from green CI alone. Native code follows GUI v0.8, not the opposite.

## License and assets

The repository contains project code and user-supplied visual PNG artwork. Retain notices and verify redistribution permission for every external visual asset, VST3 binary, JUCE/ASIO/VST3 SDK and third-party dependency before public binary release. The GUI milestone is **not a native product release**.
