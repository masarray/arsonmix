# ArSonMix – Horizontal PAN and unified tactile controls

## Design and engineering change (2026-10-25)
- 12 channel PAN knobs replaced with short horizontal rails. Supplied `PAN-SLIDER.png` (18×40 px) is used unchanged, black recessed rail and center mark rendered below it. Existing channel `pan` session property (−100 to 100) and `send('pan', ...)` protocol are unchanged.
- PAN pointer-dragging from the thumb is relative and never jumps on press. Clicking an empty part of the track moves to the requested position. Double click resets to center without opening the channel editor. Keyboard: left/right 1%, shift 10%, Page ±10%, Home C, End R100; wheel 2%.
- Five user-supplied 88×36 machined-metal PNGs are checked into `web/assets/buttons/` unchanged. Matching data URIs are embedded in CSS for offline one-file WebView2 and browser Pages parity. OFF is the neutral baseline; blue=selected navigation/action; turquoise=selected section/output; pink=streaming/talkback/presets; red=record output. Existing channel MUTE/SOLO/HPF physical buttons retain earlier 54×37 artwork.
- Covered: top navigation (MIX, FX RACK, SIGNAL FLOW, SETUP), left/right segment tabs (CHANNELS, FX CHAINS, MASTER, MONITORS, TALKBACK), output selection (Main L/R etc.), scene/snapshot controls, preset controls, FX inserts, plugin categories and selected miscellaneous actions.
- Product name now **ArSonMix** in mixer header/footer, browser title, landing page and preview shell. Internal engineering project code and historical repository filenames are preserved.
- `tests/pan_button_asset_test.py` verifies original PNG dimensions/bytes and offline CSS/JS parity; `tests/ui_smoke.py` verifies PAN pointer/keyboard/center behavior and button backgrounds. Do not merge if Chromium smoke fails.
- The preview is visual UI only; no live device signal routing or audio processing is implied.
