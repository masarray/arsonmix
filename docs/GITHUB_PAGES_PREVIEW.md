# GitHub Pages GUI preview

The website is built from the repository's actual standalone mixer source.

- Landing: `https://masarray.github.io/arsonmix/`
- GUI preview: `https://masarray.github.io/arsonmix/preview/`
- Direct fullscreen: `https://masarray.github.io/arsonmix/preview/mixer.html`

## Initial enablement

GitHub Pages must be enabled **once** by a repository administrator: **Settings → Pages → Build and deployment → Source: GitHub Actions**. If Pages is disabled, workflow uploads can succeed while the deploy action will fail. This cannot be enabled through the current GitHub connector because there is no Pages Settings write operation exposed.

Then run **Actions → Publish GUI preview to GitHub Pages → Run workflow** (choose `fix/fader-thumb-symmetry-polish` until it is merged). Afterwards, every push to the tracked GUI source branch automatically regenerates and publishes the site. The latest run's URL and success state are the authority for deployment, not guessed availability.

## Mirror contract

`scripts/build_pages.py` copies:
- `site/index.html` → root landing;
- `site/preview/index.html` → GUI preview shell;
- `AR-MIX-Live-FX-Host-Standalone-v0.3.html` → `preview/mixer.html`;
- `preview/build.json` → exact deployed git SHA and source branch.

The preview shell reads `build.json` every 60 seconds. If a new build arrives and the user is not interacting, it reloads the embedded mixer automatically. Otherwise it shows a nonblocking reload prompt. Reload now is always available.

No demo audio capture or VST3 real-time audio processing is implied. Preview is intentionally separate from public releases. To publish new GUI work from a different branch, change the workflow branch filter or merge into `main` before publishing.

## Local check

```bash
python scripts/build_pages.py --out dist-pages
python -m http.server 8000 --directory dist-pages
```

Browse `http://localhost:8000` and `http://localhost:8000/preview/`.
