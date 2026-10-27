# GUI ↔ Windows native engine reconciliation — 2026-10-27

## Current source of truth

Approved ArSonMix 10-channel GUI and calibrated fader/VU implementation is integrated on `main` through PR #80 (commit `9fb308bb67022ba691a3feddeee86972ce09071e`). GitHub Pages build.json was verified using this commit; Windows Native and Chromium CI for this exact commit succeeded.

The desktop engine does **not** use the external GitHub Pages URL or standalone file at runtime. JUCE `ARMixAssets` in `CMakeLists.txt` packages five repository files directly into the Windows GUI EXE: `web/index.html`, `web/styles.css`, `web/app.js`, `web/bridge.js`, `web/monitor.js`. `src/Main.cpp::assetFor` serves these built-in assets to WebView2. The standalone HTML on Pages and these embedded files are generated from the same mixer source.

## Release attestation strengthening

A Windows build tagged as GUI10CH must now have **all five exact UTF-8 sources embedded inside the PE32+ GUI executable**, not just contain a few old identifiers. Packaging calculates source SHA-256 for all five and stamps build provenance, and post-package verifier checks full source content within the actual EXE, source SHA-256, calibration contract, EXE SHA-256, ZIP entry SHA-256 and Windows GUI subsystem. If an older GUI is embedded under a newer `main` checkout, verification fails closed.

Source CSS `web/styles-src/42-mixer-premium.css` and compiled `web/styles.css` must match the standalone embedded CSS. Approved calibration: PNG detent y=38px with +10..−60 dB gain travel; independently calibrated VU meter area/scale with dBFS ticks; mono and stereo LED sizes uniform; 10 channels + stereo MASTER.

## Native limitations / scope guard

Only native Channel 1 and MASTER signal levels have real DSP telemetry today; 7–10 are stereo GUI strips, not independently routed physical audio channels. This reconciliation verifies UI resource identity and existing bridge behavior, **not** implementation of 10 independently routed VST3 chains. No changes to realtime audio callbacks, device routing, ASIO, sample-rate policy, or DSP processing.

## Delivery

The native Windows executable is produced by GitHub Actions `Windows Native VST3 Host` in artifact `ArSonMix-GUI10CH-P012-Verified-Hardware-FAT-Windows-x64`. Download the artifact from the successful run for the desired `main` commit. Check `BUILD_PROVENANCE.json`'s `sourceCommit`, its five resource hashes and `guiCalibration` before installing; the existing hardware FAT still applies. The artifact expires after 14 days under current workflow policy.
