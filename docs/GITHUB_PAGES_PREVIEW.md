# GitHub Pages GUI preview — production workflow

**Live and deployed** (GitHub Actions run [40147158731](https://github.com/masarray/arsonmix/actions/runs/40147158731)):

- Landing: https://masarray.github.io/arsonmix/
- Interactive mixer review: https://masarray.github.io/arsonmix/preview/
- Full-screen standalone mixer: https://masarray.github.io/arsonmix/preview/mixer.html
- Current served GUI revision: https://masarray.github.io/arsonmix/preview/build.json

## Architecture: safe GUI mirror without merging PR

**The publishing workflow lives in the default branch:** `.github/workflows/pages-publish.yml` in `main`.

The workflow **checks out `fix/fader-thumb-symmetry-polish`** to build the site, so GitHub Pages environment controls are satisfied without merging unapproved UI code into `main`.

Whenever UI smoke CI passes for the preview branch, `workflow_run` on `main` automatically triggers production Pages deployment. You may also use **Actions → Publish ArSonMix GUI (main dispatcher) → Run workflow** for manual republishing, running the workflow on **main**.

The prior branch-local `.github/workflows/pages-preview.yml` was removed because the `github-pages` environment blocked deployment from a non-default-branch workflow (build/upload succeeded but the deployment job could not start). Do not reintroduce a competing branch publisher.

## Build parity

`scripts/build_pages.py` copies:
- `site/index.html` → site landing;
- `site/preview/index.html` → review shell;
- `AR-MIX-Live-FX-Host-Standalone-v0.3.html` → `preview/mixer.html`;
- `preview/build.json` → deployed GUI git SHA + source branch.

The preview shell checks `build.json` every 60 seconds and reloads automatically if a new build is available **and the user is idle**. Otherwise, it displays a reload notice. Click **Reload GUI** to force a refresh; **Full Screen** opens the mixer independently.

## Local validation

```bash
python tests/button_asset_test.py
python tests/fader_asset_test.py
python scripts/build_pages.py --out dist-pages
python -m http.server 8000 --directory dist-pages
```

Then open `http://localhost:8000/` and `http://localhost:8000/preview/`.

The browser version is a **GUI prototype** with simulated meters; it does not capture/process real audio. Real engine work is separate.

## Future promotion

After final GUI visual approval and merge to `main`, update the checkout ref and trigger branch settings in `.github/workflows/pages-publish.yml` to publish from `main` instead. This is intentionally not done until GUI changes are approved.
