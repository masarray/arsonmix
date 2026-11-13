# Guitar FX — Production Layered PNG Contract

This is the authoritative asset contract for the **existing native JUCE** Guitar FX Rig editor. The PCM graph, A/B presets, .smix restore, and hardware input routing stay unchanged. This is not a second plugin or WebView.

## Required production layers

Place all 12 artist-approved **RGBA (8-bit, non-interlaced) PNG** images in `design-references/guitar-fx/artist-source/` using the exact following names and canvas dimensions:

| File | Canvas | Composited purpose |
|---|---|---|
| gfx-amp-head.png | 720×300 | Amp body without rotary controls |
| gfx-cabinet.png | 720×300 | Speaker cabinet body |
| gfx-pedal-gate-body.png | 300×420 | Gate enclosure and EMPTY mounting wells |
| gfx-pedal-drive-body.png | 300×420 | Overdrive enclosure with EMPTY control and footswitch wells |
| gfx-knob.png | 120×120 | Independent rotary control image |
| gfx-switch-off.png | 200×74 | Fixed-housing OFF bitmap |
| gfx-switch-on.png | 200×74 | SAME housing and alpha shape, ON bitmap |
| gfx-button-off.png | 280×80 | Shared GUI button inactive |
| gfx-button-on.png | 280×80 | Shared GUI button selected, same frame |
| gfx-panel.png | 800×330 | Empty reusable GUI chassis/background |
| gfx-stomp-off.png | 110×110 | Independent pedal switch OFF |
| gfx-stomp-on.png | 110×110 | Same geometry pedal switch ON |

**On/off anti-flicker is a hard build gate:** identical canvas; byte-identical full alpha channel; byte-identical RGB around the outer shell. Only the **internal thumb/LED/face** may change; `scripts/validate_guitar_fx_artist_source.mjs` executes at CMake configure and rejects mismatched exports before embedding. Toggle/press/hover never resizes controls; the JUCE LookAndFeel adds small dynamic visual feedback on the inset only.

**Never bake parameters into bodies.** The bodies contain mechanical wells but no working knob, no ON/OFF position, no labels, and no active DSP indicators. One composited `juce::Slider` lives over each actual DSP knob, and one `juce::TextButton` lives over the pedal footswitch. Gate has three real controls (Threshold/Hold/Release); Drive currently has one real drive control; Amplifier has three (Gain/Tone/Master); Cabinet has no invented rotary control.

## Packaging / clean-up boundary

CMake builds deterministic original PNG fallbacks when `artist-source/` is absent and embeds only `design-references/guitar-fx/assets/gfx-*.png`. All prior `pedal-chorus.png`, `pedal-reverb.png`, `pedal-delay.png`, old baked `pedal-gate.png` / `pedal-overdrive.png`, and other concept PNGs remain for the archived design prototype but are **not shipped** into the native application. Do not delete assets used by the historical prototype or other applications.

A photo-real package must be copied into the named `artist-source/` folder **and committed** before a release can claim that the generated photography-based artwork is included. A green build using only deterministic fallbacks is NOT proof the previous chat-generated photoreal source images were embedded.

## Verification

```bash
node scripts/build_guitar_fx_layered_assets.mjs
node --test tests/guitar_fx_assets_contract.test.mjs
node scripts/validate_guitar_fx_artist_source.mjs design-references/guitar-fx/artist-source
```

Then inspect a Windows JUCE screenshot for all four focuses, test independent knob rotation, ON/OFF toggle and hover/pressed states, and confirm Guitar FX remains in FX Picker. Real Hi-Z guitar/ASIO sound is a separate field acceptance.
