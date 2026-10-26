# ARI fader assets — original silver channels and gold MASTER (2026-10-26)

The final approved fader art is shipped **without resizing** or color adjustment:

- Channels 1–12: `web/assets/faders/FADER-ARI-SILVER.png`, original 36×77 RGBA, 3,801 bytes, SHA-256 `de0cd2f8250d8d4eef1ce48cb5c509f596f65134c9c618dce895eaadd54d6132`.
- MASTER: `web/assets/faders/FADER-ARI-GOLD.png`, original 36×77 RGBA, 3,997 bytes, SHA-256 `8396598e7456cb7f132c9f91ee39cb68ff7c331ae57f04aab15c259ea4143f62`.

Both supersede the discarded resized 30×70 files and the duplicate `ARI-FADER.PNG`, all removed from the PR branch. The historical red master and earlier silver art remain in the repository for baseline/rollback only; the live mixer uses silver + gold.

Authoritative CSS is `web/styles-src/41-mixer-performance.css`, with the exact PNGs embedded as static data URIs for standalone HTML and JUCE WebView2 offline parity. Only one decoded bitmap per fader color is needed; avoid new pointer event listeners, image filters, blur, hover glow, or animations.

The shared black detent occupies image rows 37–38. Render full, natural size `36px × 77px`, align with `translate(-50%,-38px)`, constrain `top` to `38px .. trackHeight-39px`, and use `Math.max(24, trackHeight-77px)` pointer travel for both channel and master. Keyboard behavior and no-jump relative drag stay intact.

Validate with `python scripts/build_ui_css.py --check`, `python tests/fader_asset_test.py` and `python tests/ui_smoke.py`. Publish GitHub Pages only through the existing passing-main workflow; do not bypass Chromium or Windows Native gates.
