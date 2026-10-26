# Reference-aligned vertical fader module (2026-10-27)

Source visual: owner supplied professional mixer screenshot alongside ArSonMix screenshot, specifically the lower vertical fader/VU section. Audit identified three structural errors: ArSonMix VU was on the right rather than left, MASTER L/R meters straddled its rail, and the dB ruler appeared on the left rather than on the rail's right.

**Uniform layout in all 11 strips:** a fixed 30px VU bank on the left, flexible central rail with the untouched 36×77 thumb, 26px right-hand graduated dB ruler with separate 5px ticks and numeric text. Bank holds a single 14px mono VU *nearest the rail*, or paired 14px L/R bars separated by 2px. MONO and STEREO share rail x-alignment. MASTER's two VUs are both left of the rail, unlike the previous split arrangement.

Preserve the already approved 10 channels (1–6 mono / 7–10 stereo), MASTER, VU shared quantized LED painter, channel color coding, mono black ink, click-to-clear clip, full-height travel, native JUCE control mapping, and all original image bytes. No extra JS repaint loops, filters, glows or animation.

Playwright acceptance tests check DOM/read order, 14px meter cases, fixed bank/ruler widths, absence of overlap, rail widths/heights and mono/stereo alignment. Since native host currently has only CH1 and MASTER live telemetry, the browser-only remaining VUs continue to be labeled simulated; this layout change is not an audio routing upgrade.

## Precise calibration: two separate rulers

The original clamp used cap top = clamp(38px, fullTrackPercent, height−39px), but gain tick labels used justify-content:space-between across the full strip. This gave visibly false dB indications at both ends and intermediate positions.

**Gain scale:** the travel box is inset 38px from top and 39px from bottom (original 36×77 PNG with black center stripe at pixel 38). Its movement is H−77px. A cap at +10 dB has black stripe exactly on +10 tick; at −60 dB black stripe exactly on −60 tick. All intermediate positions use the same levelToTop gain fraction, 10..−60 dB; cap and tick are both absolute inside the same travel. Existing drag (H−77px) and keyboard control mapping remain identical.

**VU scale:** a separate 0..−60 dBFS ruler is aligned to the **active LED signal area**, not the clip indicator or gain thumb. Each 14px bar's active area starts 12px below meter top and ends 2px before meter bottom. The 0, −6, −12, −18, −24, −36, −48 and −60 dBFS labels use that same range. Native bridge already maps peak amplitude with (20·log10(peak)+60)/60 to normalized percent; this ruler preserves that calibration. Browser simulation remains marked UI Preview.

The meter fill and held peak now share a single positioned signal-area origin. The fixed multicolor LED paint, 7px quantization, L/R layout, clip reset, original fader PNG bytes, and native DSP remain unchanged. A Playwright pixel test checks cap black stripe versus tick and active LED region versus VU tick at 11 strip heights, including +10 / −60 endpoints.
