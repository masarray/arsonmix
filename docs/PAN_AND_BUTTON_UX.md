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

## 2026-10-25 — Soundcraft reference / Studio One style insert-power refinement

- **PAN**: added center-origin illuminated green/cyan ribbon between center tick and metallic PAN thumb, positive and negative pan; zero is empty, fully pan is fully illuminated. Existing pan range, no-jump drag and keyboard behavior remain.
- **Master MUTE**: fixed physical printed label `MUTE` regardless of state; red artwork + pressed state mean muted. ARIA pressed persists.
- **FX insert**: occupied rows now have separate **FX name/edit target** and **⏻ power target**. Button never opens browser, and opening name never changes bypass. Gray power = bypassed, luminous aqua = on. No power for empty slot. Existing chain bypass mechanism is the single state; it syncs main strips and full FX Rack, sends existing `chainBypass` bridge event, and immediately persists the UI/session state. This is a demo: audio DSP bypass still requires the native host to consume this event.
- **Persistence**: initial restored session now also restores `bypassed`, `fxParams`, and `masterMute`.
- **Tactile feedback**: increased hover brightness/contrast for hardware controls; darker active press. The 31×90 original fader image, exact translation/geometry, and hit zone remain unchanged.
- **Geometry & regression guard**: Chromium checks pan bar left/right/center, 60 compact 25px rack slots, two independent FX targets, synchronized bypass, unchanged MUTE caption and visible hover feedback.
- **Audit priorities**: use color only for meaningful state, keep signal-chain names legible at 1440px, isolate native DSP functions from preview simulation, and prevent per-strip overflow rather than scaling all buttons globally.

## October 25, 2026 — Studio One-inspired channel/master metering and channel identity

- PAN: removed neon glow from metallic thumb and green direction ribbon. Pointer interaction snaps to the center when the requested value is within ±6 units; keyboard retains fine one-unit increments. PAN is still -100..+100 and double-click centers.
- Fader/button hover: reduced previous exaggerated contrast and brightness, keeping recognizably physical pressed states.
- Insert rack: removed occupied slot `◉` graphic entirely; empty slots have one `＋` glyph and caption `ADD FX` (no repeated plus).
- Channel identity: clicking the bottom colored identity plate opens a compact console-themed editor: channel name (26 characters maximum), twelve icon choices, twelve semantic strip colors. Identity is saved in UI session and snapshots and restored on reload. Values are escaped/whitelisted; cancelling discards changes.
- Focused channel: border follows selected or interacted-with strip from channel header to bottom plate without changing fader geometry.
- Input meters: industrial tick marks, red peak hold line (1.6 s), 8px raised hardware-style clip candy LED with latched red state and per-channel click-to-clear.
- Master strip: own 5-slot UI FX chain (4 visible in Easy), dedicated enable/bypass buttons, dual L/R meters each with individual clip and peak, all-clip reset, exact original FADER-RED2.png (31×90), and output telemetry. Master fader's start is vertically aligned to input faders in both Easy/Pro UI modes.
- Engineering integrity: source red master PNG git blob SHA `2369e1b9002522600948909246918e57880da1ed` is checked in. A previous one-character base64 corruption caused the image to be invisible; tests now verify byte-exact embedding and `Image.decode()` of 31×90 in Chromium.
- The preview's `LUFS-I`, `dBTP`, and `PEAK` displays are visibly marked `SIM`/`DEMO · NOT CALIBRATED`. They are **not** real BS.1770 LUFS or oversampled true-peak measurements; those must be sourced from DSP/native audio metering before the product claims compliance.
- Browser FX power (including master) is a GUI state/bridge event only; real VST3 bypass requires the native host to consume and apply the changes safely at audio-buffer boundaries.
- QA: 12 input strips, 13 faders total, dual master meters, 14 clip LEDs, original master PNG decode, channel rename/icon/color persistence, FX bypass, no layout overflow at 1680×920 and 1440×820, plus aligned Easy/Pro fader-start geometry.
