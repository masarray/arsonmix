# RAIL1 historical correction

RAIL1 (PR #213) was rejected by the operator after screenshot inspection: a static teal full-height guide illuminated the upper black groove, unlike the supplied fader reference. Superseded by [RAIL2](RAIL2_REFERENCE_BLACK_TOP_BLUE_BOTTOM_20261031.md). Keep this as background audit, not an active UI specification.

# RAIL1: single machined fader slot with static cyan-teal guide

Source baseline: `59b81e51e16e35cebf165a6ce711ff750741c15f`, 31 October 2026. GUI-only milestone; preserves L2 #176 and GUI-M1 #211.

## Verified root cause

Historical M4/M5 `fader::after` painted two layers. A topmost **vertical** transparent-to-opaque mask controlled by `--fader-open` covered the horizontal blue gradient above the thumb but revealed it below. That gave the one slot two different material treatments, especially at intermediate gains. Every drag and three native readback paths updated the variable even though the cavity is stationary.

## Visual contract

- 11 faders (10 channels plus MASTER) share the same `::before` recessed graphite well and `::after` 5px matte-black slot.
- One static **horizontal** gradient in the slot, dark symmetric walls with a very narrow cyan-blue/teal `#299f9d` centre. No vertical fill/mask, no gradient controlled by cap position, no blur/glow filter, no new animation.
- Approved channel silver and MASTER rose-gold 36x77 PNG assets remain identical. The cap `--pos` moves, rail does not. Do not change detent at 38px, +10/-60 gain span, VU ruler or meter calibration.
- Remove unneeded `--fader-open` per-gain style writes in preview and 3 native readback paths. No added DOM, listeners, RAF, workers, GPU layers, DSP path, ASIO or VST state changes.
- Source-owned `web/styles-src/42-mixer-premium.css`; compile canonical `web/styles.css`; update embedded standalone HTML. No forked styles.

## Verification and release gates

Run `python scripts/build_ui_css.py --check`, `node --test tests/mixer_shell_contract_test.mjs`, `python tests/mixer_shell_geometry_smoke.py`, `python tests/ui_smoke.py`, `python tests/mixer_io_smoke.py`. Validate computed static slot paint before/after channel and MASTER fader travel, 11-strip geometry across FX/ROUTE/SENDS × four widths, screenshot at 1366 and 1680, and no JS console errors. Production hardware FAT #15, Windows GPU pointer responsiveness and any exact physical latency claims remain separate.

References: [MDN CSS gradient stacking](https://developer.mozilla.org/en-US/docs/Web/CSS/Guides/Images/Using_gradients), [MDN CSS performance](https://developer.mozilla.org/en-US/docs/Learn_web_development/Extensions/Performance/CSS).
