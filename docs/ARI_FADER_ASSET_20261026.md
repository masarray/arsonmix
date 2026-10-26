# ARI fader art — silver channels / gold MASTER (2026-10-26)

Owner-uploaded PNGs: silver `FADER-ARI (1).png` (33x79 RGB, SHA-256 `f3820891ffa019f899a079b8d0cb7cf7726fc757155c32fc20022c21cfacfccf`); gold `FADER-ARI (2).png` (33x79 RGBA, SHA-256 `839a2d41efb07e2d2fa252be80bf86f43876b8b291c0e4568bd9c14eba5413e3`).

Offline 30x70 downsampling avoids continuous browser image scaling. Production PNGs: `web/assets/faders/ARI-FADER-SILVER-30x70.png` (indexed 256-color silver, 2925 bytes, SHA-256 `88f3e7932466b12fbcffe8fb69353371496daea32fa19cda88dc413516d4bb34`), `web/assets/faders/ARI-FADER-GOLD-30x70.png` (RGBA gold, 4357 bytes, SHA-256 `8bc265a895de882842817275f439bcbedfd117ade62ea142a71cc3ce3b33eb59`). Older silver/red art remains stored for rollback but is superseded in the live mixer.

CSS owner: `web/styles-src/41-mixer-performance.css`. The channel PNG is fixed through `--ari-channel-cap`; MASTER uses its existing `--master-cap-asset` variable replaced by the gold URI in the last CSS module. Both render at native 30x70 pixels with image detent at y=34; clamp the pixel origin at 34px..height-36px to prevent clipping; drag travel = track height minus 70px. Preserve keyboard/aria, fixed-color LED meter, no-blur state and VST3 audio path. No new event listeners, visual hover filters or animations.

Regenerate/check CSS and standalone HTML with `python scripts/build_ui_css.py --check`. Regression guards in `tests/fader_asset_test.py` and `tests/ui_smoke.py` verify source hashes, unique silver/gold renderers, coordinates and pointer movement.
