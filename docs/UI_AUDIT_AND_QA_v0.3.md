# AR MIX Studio v0.3 — GUI Audit, Diff and Evidence

**Source audited:** v0.2 interactive HTML and v0.2 JUCE starter ZIP.  
**Target baseline:** user's approved industrial/contemporary console mockup, with dedicated VST FX host workflow.  
**Validation environment:** Linux Chromium headless, viewport 1680×920, JS syntax check, g++ C++20 for isolated RT controls.

| Audit ID | v0.2 finding | v0.3 change | Tested |
|---|---|---|---|
| V-01 | Selected CHANNELS/MIX/MASTER glossy surfaces have sharp midline/seam | New continuous 3D highlight and subtle top reflection | Visual screenshot |
| V-02 | Fader head clipped/overlapped at upper range; travel CSS invalid in Chromium | Percentage travel + `clamp(25px, var(--pos), 100%-25px)` maintains thumb clearance | Screenshot: faders at different y positions |
| UX-01 | Plugin slots are decorative / hard to browse | Primary FX RACK screen plus modal search and category filter; add/reorder/bypass/remove simulated FX | Automation clicks |
| UX-02 | Mixer feels like full engineering console | Easy default + Pro toggle, reduce trim clutter, keep PAN/MUTE/SOLO, rack/faders visible | Automation toggle |
| UX-03 | No beginner routing explanation | 4-stage Signal Flow screen, setup/audio safety copy | Automation navigation |
| UX-04 | Presets and sessions vague | Quick vocal chain demo, FX preset and Mixer Snapshot storage, JSON export/import | Scripted UI navigation; manual file dialog not validated |
| P-01 | All meters animated every browser frame | 30Hz visibility-throttled simulated metering loop | Source audit; no real GPU benchmark |
| RT-01 | No tested RT-safe primitives in shell starter | `SpscQueue` + `ParameterBank`, explicitly SPSC and scalar coalescing | 250k events, wraparound, last-write tests |

## Browser smoke test details

- Render: 12 channel strips, Easy default, all toolbar surfaces rendered.
- FX Rack: open and show chain, add searched De-Esser, bypass, reorder, chain remains consistent.
- FX editor: generic mock Mix parameter edited from default to 72; state follows plugin after reorder.
- Signal Flow: four route nodes visible.
- Setup: four instructional blocks visible.
- Easy/Pro: toggled successfully.
- JavaScript page errors: none after patching the removed hidden `groupsList` container.
- Render screenshot: `docs/AR-MIX-v0.3-Mixer.png`
- FX Rack screenshot: `docs/AR-MIX-v0.3-FX-Rack.png`
- Signal Flow screenshot: `docs/AR-MIX-v0.3-Signal-Flow.png`

**Not yet verified:** Windows 125/150% display scaling; native WebView2 shell compilation; real VST3 scan; FX plugin state persistence; real audio capture/output; buffer underrun / callback budgets / device RTL; production memory leak analysis. Treat these as explicit release gates rather than claimed capabilities.

- Responsive QA: at 1366×768, 1920×1080, 2560×1440, measured 12 strips, **zero fader head geometry clips** and **zero JavaScript page errors**. Screenshots were inspected.

## 2026-10-25: Mirror-symmetric fader polish

- Root cause: original 45px head used a fixed 19px centerline. An overriding 39px height and biased shadow left the cap unbalanced, while an obsolete travel calculation remained.
- Repair: single 29×42px metallic head, matching 5px top/bottom corners and centered black stripe; the lower grip is a true Y-mirror of the upper grip. Same CSS for channel and master.
- Keep the cross-browser 25px safe-travel clamp. Add subtle hover and keyboard focus highlights; remove contradictory height/position declarations.
- UX: vertical ARIA sliders, live master value, 0.5dB arrow steps, 5dB Page steps, Home (-60dB) and End (+10dB).
- Automated Playwright smoke includes cap dimension, pseudo-layer symmetry, boundary clipping at initial positions, keyboard endpoints and existing FX workflows. Only the standalone component has been visually rendered in Linux Chromium during this patch; run the complete smoke suite before release.

## 2026-10-25 — Image-backed rugged silver fader (supersedes flat CSS v0.3)

- **Canonical reference:** `web/assets/fader-silver2.png` (user-supplied Gambar 3, 31×90 RGBA, SHA-256 `0f45fe5aebc8db7b65c4163ffad083cbe3965d59cde7056add94812ac067a899`).
- The original PNG preserves curved machined-metal edge shading, fine ridges and soft downcast shadow. The embedded CSS data URI uses exactly the same PNG so **standalone HTML, normal browser CSS and JUCE WebView2 never depend on external file loading**; the canonical PNG remains replaceable.
- On-image black horizontal center mark is **pixel y=35**, CSS `translateY(-35px)` pins that mark to the fader dB coordinate. The body ends near y=65; the transparent falloff continues below without changing the controlled value. Keep the visual clipping assertions limited to the actual metal body (not its cast shadow).
- Pointer-down on an existing cap now retains the current gain; drag is relative to the starting value. Track click still jumps as before. Keyboard and ARIA controls retained.
- QA: `python tests/fader_asset_test.py` checks byte-exact PNG, CSS and offline trial synchronization. Chromium `tests/ui_smoke.py` covers asset decode, all 13 cap dimensions, body clipping, no-jump drag, keyboard endpoints, plugin rack operations and screenshot artifact.
- **Known:** Original user asset is only 31px wide. It is deliberately shown at approximately original pixel dimensions, not artificially upscaled. Replacing the original source with a high-DPI recreation may be considered later if 150%/200% scaling reveals softness.
