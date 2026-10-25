# ArSonMix public Pages — approved GUI v0.8 runbook

**The approved GUI lives in `main`**, merged via [PR #2](https://github.com/masarray/arsonmix/pull/2); original approved commit: `343692fc9b702661a8455cca30f0ffd108907324`.

- Landing: https://masarray.github.io/arsonmix/
- Live mixer: https://masarray.github.io/arsonmix/preview/
- Full-screen mixer: https://masarray.github.io/arsonmix/preview/mixer.html
- Deployed SHA: https://masarray.github.io/arsonmix/preview/build.json

## Publishing pipeline after approval

1. Contributions to GUI files on `main` trigger `.github/workflows/ui-smoke.yml`.
2. Only a successful **`main` UI smoke** triggers `.github/workflows/pages-publish.yml`.
3. Pages publisher checks out the **exact successful smoke-run head SHA**, not a mutable stale GUI branch or an untested newer commit. A manual dispatch builds current `main`.
4. `scripts/build_pages.py` copies `site/` and the canonical single-file standalone to `preview/mixer.html`, then writes `build.json` with SHA/branch.
5. Preview shell automatically reloads when idle and a newer SHA is deployed; it does not add a second application header.

Only one production Pages workflow is authoritative. Do not introduce a parallel branch-local publisher or restore checkout of `fix/fader-thumb-symmetry-polish`.

## Local tests

```bash
node --check web/app.js
python tests/fader_asset_test.py
python tests/button_asset_test.py
python tests/pan_button_asset_test.py
python tests/gui_baseline_lock_test.py
python tests/ui_smoke.py
python scripts/build_pages.py --out dist-pages --sha local --branch main
python -m http.server 8000 --directory dist-pages
```

Open `http://localhost:8000/preview/` and compare against [GUI baseline lock](GUI_BASELINE_LOCK_2026-10-25.md).

## Scope / honesty

The GitHub Pages console remains a **browser UI prototype**. VU meter animation, LUFS-I and dBTP are simulated, not authentic native DSP readings. It does not enumerate or process installed VST3 plugins. Native integration is a separate, unmerged engineering stage and requires Windows hardware/latency and plugin verification.
