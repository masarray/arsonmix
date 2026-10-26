# Reference-aligned vertical fader module (2026-10-27)

Source visual: owner supplied professional mixer screenshot alongside ArSonMix screenshot, specifically the lower vertical fader/VU section. Audit identified three structural errors: ArSonMix VU was on the right rather than left, MASTER L/R meters straddled its rail, and the dB ruler appeared on the left rather than on the rail's right.

**Uniform layout in all 11 strips:** a fixed 30px VU bank on the left, flexible central rail with the untouched 36×77 thumb, 26px right-hand graduated dB ruler with separate 5px ticks and numeric text. Bank holds a single 14px mono VU *nearest the rail*, or paired 14px L/R bars separated by 2px. MONO and STEREO share rail x-alignment. MASTER's two VUs are both left of the rail, unlike the previous split arrangement.

Preserve the already approved 10 channels (1–6 mono / 7–10 stereo), MASTER, VU shared quantized LED painter, channel color coding, mono black ink, click-to-clear clip, full-height travel, native JUCE control mapping, and all original image bytes. No extra JS repaint loops, filters, glows or animation.

Playwright acceptance tests check DOM/read order, 14px meter cases, fixed bank/ruler widths, absence of overlap, rail widths/heights and mono/stereo alignment. Since native host currently has only CH1 and MASTER live telemetry, the browser-only remaining VUs continue to be labeled simulated; this layout change is not an audio routing upgrade.
