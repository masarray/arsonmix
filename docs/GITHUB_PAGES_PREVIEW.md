# ArSonMix GitHub Pages — ONE approved mixer GUI

**The ONLY approved GUI route:** https://masarray.github.io/arsonmix/preview/ .

The user explicitly rejected bare `/preview-v2/` as product GUI. The approved `/preview/` visual includes the special PNG-based MUTE/SOLO/PFL hardware keys. The publishing process assembles current internal mixer HTML/WIRE/FX/fader/meter sources with these specific key assets, and publishes **one** direct page at `/preview/index.html` with matching `/preview/assets/**`.

The following old preview URLs are **redirect-only**, never independent mixer renderers: `/preview-v2/`, `/preview-buttons/`, `/preview-v33/`, `/preview-v34/`, and `/preview/mixer.html`.

## Source and publishing
- `scripts/build_pages.py` combines the shared internal HTML seed `site/preview-v2/index.html` and `site/preview-v2/assets/**` with explicitly user-approved `site/preview-buttons/assets/mixer-control-keys.css`, `mixer-control-keys.js` and `mute-solo-pfl-56x37.png`.
- These directories are **internal build dependencies**, not additional published GUI variants. Refactor/rename only with byte-for-byte visual and interaction parity, not by switching to bare preview-v2 styling.
- Pages workflow `.github/workflows/pages-publish.yml` publishes from the qualified `main` and runs `python tests/single_preview_publication_contract_test.py dist-pages`. That test ensures only /preview/ renders mixer HTML, verifies special key and WIRE material, and requires old routes to redirect.
- The landing `site/index.html` must link and embed ONLY `./preview/`; no links to competing GUI paths.

## Windows native product
ENGINE #266/#387 must be stitched beneath the exact **assembled** /preview/ renderer via one JUCE WebView2 native adapter, no redesign. P0 [#389](https://github.com/masarray/arsonmix/issues/389) owns native GUI integration; FAT #270 must verify Windows EXE source/asset hash and screenshot/interaction parity plus actual PCM/VST3. `node scripts/check_console_puzzle.mjs --release` is fail-closed until actual compiled GUI and one engine are verified.

**The Pages UI is a simulation and not proof that hardware audio, VST3 or PCM is production-ready.**
