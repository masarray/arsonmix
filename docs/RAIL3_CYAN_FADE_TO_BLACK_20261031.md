# RAIL3 — cyan under-thumb bloom, black at fader foot (2026-10-31)

Based on user review of RAIL2 / PR #214. **Do not change the cavity geometry** or the upper black slot.

The physical 5px machined slot remains uninterrupted. The upper section is black without light. Immediately below the cap, light emerges gradually from dark slate to cyan `#27bfd8` over about 42px. Toward the lower end (62–100% of track), an independent static black attenuation overlay progressively dims the cyan: 10% black at 72%, ~52% at 86%, ~85% at 94%, and opaque black at 100%. Dark inset sidewalls remain visible at every height. The result is an optical reflection inside a black channel, not a flat neon progress bar.

Only CSS source `web/styles-src/42-mixer-premium.css` changes in runtime. The three CSS gradient layers are: (1) fixed left/right sidewall, (2) fixed bottom attenuation, (3) existing `--fader-cut`-synced upper-black / under-thumb cyan emission. No new DOM, state, pointer event, timer, RAF, filter, blur, shadow animation, worklet, worker, or C++/ASIO/DSP changes. Original 36x77 PNGs, 38px detent, gain mapping, left meter/ruler, 11-strip grid and MASTER pin are preserved.

Quality gates: source/compiled/standalone CSS byte parity, original fader asset hashes, 3-layer CSS contract, computed cyan and lower black attenuation in CH and MASTER at -60/+10, Chromium fader+geometry+FX/routing UI smoke, screenshot inspection at 1680 and 1366. Do not merge on CI failure. Integrated GPU/WebView2 real-hardware smoothness and zero-FX crackle issue #15 remain separate field tests.
