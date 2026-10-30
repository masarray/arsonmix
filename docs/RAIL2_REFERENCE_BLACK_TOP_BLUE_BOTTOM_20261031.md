# RAIL2 — black above the thumb, smoothly illuminated steel-blue below

Date: 2026-10-31. This is the operator's explicit correction to RAIL1 #213, not a feature redesign.

## Observed reference (user-supplied image 2)

- Upper portion from the top of the travel to the bottom edge of the silver thumb is a dark, unilluminated black slot.
- A **steel-blue light guide**, approximately `#3576B1` (sampled on the screenshot's lower rail), is present **only below the cap**, with a transition near the thumb from deep shade to stable blue over ~40 pixels. No green/teal cast above the cap.
- The physical well and black sidewalls are the same geometry above and below. Source/MASTER silver/rose-gold 36x77 thumb images stay unmodified.

## Implementation contract

- Existing `fader::before` graphite machined well remains unchanged; `fader::after` keeps the existing 5px continuous slot. Compose two simple layers: static horizontal dark sidewall shading and a **variable vertical illumination gradient** whose threshold equals the cap's existing detent percentage + 26px; its saturated blue is reached near detent + 68px. This ensures upper black and an under-cap dark-to-blue ramp.
- The host CSS custom property `--fader-cut` is set in initial markup, updated in the **existing** channel / MASTER UI gain handler, and synchronized in the three native gain ACK/readback branches. This incurs a 5px CSS repaint as needed for accuracy, not a new animation clock, worklet, canvas, worker, GPU layer or extra DOM node. The cap remains moved by `--pos` exactly as before.
- No effect on audio gain values, VST3 slots, routing, pan, fader 38px detent, meter ticks, linear dB mapping, L2 seven-row grid or pinned MASTER.
- Preserve canonical CSS ownership and source parity for standalone HTML and native WebView2.

## Acceptance

CSS computed style must have **two** linear gradients; fixed horizontal walls, and a vertical gradient that stays black before the cap, darkens/brightens gradually after cap, and reaches `#3576B1`. Fader cutoff reaches 0% at +10dB, 100% at -60dB in channel and MASTER; PNG cap `--pos` and optical `--fader-cut` remain identical after UI events and native readback. Test source parity, screenshots at 1680/1366 and L2 geometry at four sizes. Use one bounded Chromium CI on this PR only, not Windows hardware CI. Windows integrated-GPU and ASIO hardware still require field FAT.

The source screenshots are visual references; exact proprietary hardware materials and pixel equality are not claimed.
