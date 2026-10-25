# ArSonMix – Horizontal PAN and unified tactile controls

## Design and engineering change (2026-10-25)
- 12 channel PAN knobs replaced with short horizontal rails. Supplied `PAN-SLIDER.png` (18×40 px) is used unchanged, black recessed rail and center mark rendered below it. Existing channel `pan` session property (−100 to 100) and `send('pan', ...)` protocol are unchanged.
- PAN pointer-dragging from the thumb is relative and never jumps on press. Clicking an empty part of the track moves to the requested position. Double click resets to center without opening the channel editor. Keyboard: left/right 1%, shift 10%, Page ±10%, Home C, End R100; wheel 2%.
- Five user-supplied 88×36 machined-metal PNGs are checked into `web/assets/buttons/` unchanged. Matching data URIs are embedded in CSS for offline one-file WebView2 and browser Pages parity. OFF is the neutral baseline; blue=selected navigation/action; turquoise=selected section/output; pink=streaming/talkback/presets; red=record output. Existing channel MUTE/SOLO/HPF physical buttons retain earlier 54×37 artwork.
- Covered: top navigation (MIX, FX RACK, SIGNAL FLOW, SETUP), left/right segment tabs (CHANNELS, FX CHAINS, MASTER, MONITORS, TALKBACK), output selection (Main L/R etc.), scene/snapshot controls, preset controls, FX inserts, plugin categories and selected miscellaneous actions.
- Product name now **ArSonMix** in mixer header/footer, browser title, landing page and preview shell. Internal engineering project code and historical repository filenames are preserved.
- `tests/pan_button_asset_test.py` verifies original PNG dimensions/bytes and offline CSS/JS parity; `tests/ui_smoke.py` verifies PAN pointer/keyboard/center behavior and button backgrounds. Do not merge if Chromium smoke fails.
- The preview is visual UI only; no live device signal routing or audio processing is implied.

## 2026-10-25 — Context-aware physical-button refinement

**Root cause:** two appended CSS blocks applied 54x37 and 88x36 PNGs to unrelated elements using a broad `:is()` selector, stretching the hardware artwork and inflating the FX rack and small toggle controls.

**Fix:** delete those blanket overrides, restore original slim recessed FX inserts and compact icon controls, retain 18x40 PAN slider and existing 31x90 metal faders. New families are applied **per functional region**, and the original button images are checked in under `web/assets/buttons/`:

| GUI region | Correct art | State |
| --- | --- | --- |
| Dense channel strip MUTE/SOLO/HPF/phase/A-B | SO-S-*.png (49x37) | silver off, red mute, yellow solo, green HPF, blue phase/routing |
| Top navigation + left/right tabs + output destinations | SO-M-BTN-*.png (88x36) | neutral off, blue current view, turquoise selected section/output, red selected record, pink selected streaming |
| Sidebar quick sound presets | SO-L-BTN-*.png (192x36) | dark off, turquoise active |
| Talkback large action | SO-B-BTN-*.png (88x56) | dark off, blue selected |
| FX insert rack | Existing original recessed CSS | No button art; maintain compact signal-chain presentation |
| Tool icons and plugin category controls | Existing original UI | No global material overlay |
| Master identity plate | Existing dedicated plaque styling | Not a button |

All skins are data-URI embedded from **byte-identical original PNG assets**, so standalone WebView2 and GitHub Pages share one rendering path. No JavaScript audio routing changes.

**QA:** `tests/button_asset_test.py` checks all nine new families, image sizes/byte integrity and absence of the obsolete global override. `tests/ui_smoke.py` now verifies that four families remain different, FX slots and tool icons do not accidentally inherit large button images, strip switches do not overflow, and review screenshots are captured at 1680x920 and 1440x820. The visual regression artifacts are attached to successful CI runs.
