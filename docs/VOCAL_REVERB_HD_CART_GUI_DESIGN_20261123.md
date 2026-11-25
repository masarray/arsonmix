# Vocal Reverb GUI design: HD-CART-inspired native JUCE console
Date: 2026-11-23. Reference: owner supplied eight plugin screenshots; winning layout reference is HD-CART (first image, 1200 × 600 jpeg).

## Selection and original-product boundary
HD-CART is the strongest **ergonomic composition** for ArSonMix live vocal reverb: large persistent REV TIME and DRY/WET, one central preset/name strip, stereo meter, and compact grouped detailed controls. Verbsuite Classics is a secondary reference for tactile metal dial construction, not a second UI to replicate. Supernova has attractive atmosphere but excessive physical occupancy for the current native FX slot. The Plate is elegant but only five exposed controls and too little for 13-parameter ArSonVerb.

This ArSonMix implementation does **not** embed HD-CART art, product names, logos, manufacturer identity, screenshots or knob raster assets. An original vector look-and-feel follows its **spatial control grammar**, not its branded trade dress: charcoal metal chassis, recessed midnight-blue header, warm-gray dial face and cyan segmented arc.

## Native editor geometry (1120 × 430 logical JUCE pixels)
- Outer cabinet: 9,9→1111,421, bevel radius 7; header band y11–69, body y70–355, digital/status footer y355–408
- Preset navigation: previous x383–420, editable factory preset combo x432–688, next x700–737, y18–54
- Left hero Reverb Time: slider x48–233 / y175–355, persistent across every tab
- Right hero Dry/Wet Mix: slider x889–1073 / y175–355, persistent across every tab
- Central tab row: four buttons x299 + 142 × index, y168–199
- Five secondary controls: slider x278 + 115 × index, y227–335; each with **genuine numeric value textbox**, rotary mouse/keyboard manipulation and double-click factory default
- Central real PCM meters: INPUT and OUTPUT, two channels each, 20 segments; read one atomic telemetry snapshot per 30 Hz GUI timer (no audio-thread UI calls)
- Native PCM status: APPLYING / QUEUED / PCM OK / OFFLINE, driven by accepted audio callback ACK; never pretend the FX applied while the SPSC queue is pending

## Complete parameter exposure, no dead controls
- Main always-visible: DecaySeconds (Reverb Time) and Mix (Dry/Wet)
- MIX: PreDelayMs / EarlyLevel / Width / LowCutHz / HighCutHz
- SPACE: PreDelayMs / Size / Diffusion / EarlyLevel / Width
- TONE: DampingHz / LowCutHz / HighCutHz / EarlyLevel / Width
- MOTION: ModDepthMs / ModRateHz / DuckAmount / Size / Diffusion
- Algorithm combo: Classic, Plate, Chamber, Hall; eight singer presets and custom state, original stable parameter IDs untouched

## Product/realtime acceptance
- Source of truth remains `VocalReverbProcessor.h`; no independent JS-audio engine, WebView clone, new callback, duplicate PCM graph, VST3 GUI fork or global /preview/ redesign.
- Renderer and all controls belong to the existing JUCE message thread, never allocate/lock in ASIO PCM thread. Audio publishes only four float peak atomics with bounded two-channel peak scans. GUI repaint is limited to meters at 30 Hz when possible.
- The original upstream DSP, preset rules and `.smix` ACK state semantics are unchanged. Any future addition of tail/early-specific meters must expose a real DSP telemetry signal, not animate a decorative dot display.
- Source-level pixel bounds above are **coded**; visual pixel parity to the selected screenshot is NOT established until a native Windows screenshot and high-DPI, keyboard, drag, host window close/reopen and narrow-resize FAT are reviewed. The UI cannot truthfully be called 'pixel perfect' before that review. No logo/trade dress copied.


## HD-CART screenshot geometry audit — 24 Nov 2026
**Evidence:** owner-provided source image `HD_Cart_Store_Image.jpg`, raw 1200×600; the actual plugin panel spans x20–1180 (1160 px), y78–522 (444 px), panel ratio **2.613:1**. Previous ArSonMix JUCE screenshot is 1120×430, ratio **2.605:1**, so proportions are already close (0.3% ratio difference) but the **content placement and rendering** are not. This is a visual, not copied-image, implementation.

### Quantified gap and implemented correction
- In HD-CART the distinctive **navy top strip** runs behind a **curved grey double shoulder**; old ArSonMix used a uniform rectangular navy box. New original JUCE vector `Path::cubicTo` uses a shoulder at y31 and a recessed preset bay at y59 with paired symmetric cubic curves.
- Source Reverb Time / Dry-Wet ring centres approximately at (145,242)/(1015,242) in panel-local coordinates (source pixels less x20/y78); new ArSonMix hero sliders allocate x48–233 and x886–1071, y108–398 including footer text, with dial centre from the actual JUCE rotary draw area (not hardcoded visual image).
- Source tabs are compact text links around panel-local y200–218 with a hairline underline, not large rectangle buttons. New tabs use six actual functional groups at x288+94*i, y183–219: MIX/SHAPE/SPACE/TONE/MOTION/DUCKER. All 13 stable DSP parameters remain controllable, with persistent hero parameters.
- Source five smaller dials align between x~310–815 below tabs. New x277+115*i, with a native Slider whose dial is visually positioned between tab underline and the blue LCD footer.
- Source readouts are concise **unit-bearing** illuminated digital strings in the blue footer; old native JUCE showed e.g. `2.0500000`, `52.000000`, `14200.000000` inside boxes under the knobs. The new display and reverse text parser use exact underlying DSP units; values are formatted `2.05 s`, `52 ms`, `18 %`, `14.2 kHz`. The parser preserves automation semantics, and the visual LCD text is still an editable real Slider, not a decorative label.
- Source knobs have brushed graphite faces, a shadowed hardware bezel, separated icy-blue LEDs and small white index dot. New `DialLook` uses per-knob original vector bevel/elliptical gradients, 42 hero and 25 secondary discrete LED segments, and a data-correct moving index.
- The original reference has label/readout separation and no extraneous duplicated copy under the small controls; the new editor has removed redundant footer help prose and moved the real numeric sliders onto the bottom band. Brand and icons remain independently authored as **ARSONMIX / AR-VERB**, not any Reverb Foundry or LiquidSonics logo/artwork.
- Input/output LED bars are read exclusively from four **PCM peak atomics** and add no scan when no native editor is open. Never simulate active bars on an inactive processor just for visual similarity.

### Quality and acceptance
- Source of truth: native `VocalReverbEditor.h`, no independent WebView, JS PCM DSP, standalone mock screenshot, or second ASIO driver.
- Actual Windows test must show source dimensions 1120×430 and 150% DPI 1680×645, tab visibility/clicks, hero persistence, valid value formatting and parse round-trip, and that header/footer hues differ from the body.
- **No literal claim of "100% pixel-perfect"**: the vendor's assets and branding are intentionally not copied; owner should assess the screenshot's visual likeness and editability after native JUCE screenshot artifact is produced. The exact-DSP AVRB state format and production FX catalog ID remain unchanged.
- A targeted Windows native screenshot test is necessary but **not sufficient**: independent native-full/ASIO FAT, release CI, real vocal audition still guard integration into INT-1.

## Calibrated after first REAL 86295ec Windows screenshot (24 Nov 2026)
The first exact-SHA Windows MSVC compile **passed**, and its actual renderer exported 1120×430 + 1680×645 PNG; the test subsequently failed because a prior MOTION assertion still expected DUCKER control to be on that page. This is a test contract/page mapping bug, not an audio engine crash. Test now checks MOTION has modulation controls, and switching to the distinct DUCKER tab exposes Parameter 12.

Image-derived mean color on matched regions (source image cropped to original 1160×444 housing, ArSonMix 1120×430):
| Source region | Owner HD-CART RGB | old native RGB | first new native RGB |
|--|--|--|--|
| Body center | (59,59,60) | (39,48,56) | (43,46,51) |
| Bottom LCD | (26,70,104) | (8,34,55) | (12,42,66) |
| Header/preset bay | (34,60,81) | (22,41,57) | (20,48,70) |

The first structural reconstruction improved correct geometry but was still **noticeably too dark**. The next native revision raises the metal body's tonal values and calibrates the LCD / blue header toward measured source contrast without embedding any source texture or protected branding. The actual Windows screenshot is re-validated, never guessed pixel-perfect.

**Visual defect corrected:** the Plate algorithm dropdown at (939,120) visually overlapped the Dry/Wet hero label. It now occupies the space under the original AR-VERB label at y95–119; the hero caption begins y125. The native CTest adds a non-overlap invariant. The cramped half-visible text in the 10 px bottom cabinet lip is removed, and editable JUCE Slider numeric values sit directly on the continuous blue LCD rather than seven dark rectangular boxes.

**Functional visual evidence:** the screenshot test renders meters with 16 blocks of authentic, bounded, stereo PCM against a separate native processor instance, then allows the existing 30 Hz UI timer to sample audio-owned float peak atomics. The main DSP/state/ACK test processor remains pristine; screenshot pulses do **not** count as a simulated ASIO hardware test. No fake illuminated LED or extra callback is added. The test asserts bright-vs-dark segments, each real tab's active controls, stable hero controls, 100% and 150% editor snapshots, and DSP parameter text round-trip.


## REVERB-SIS brand cleanup and six-page collision gate — 24 Nov 2026
The owner reviewed the ACTUAL Windows JUCE screenshot from SHA 7d466711 and expressly requested a premium, minimalist faceplate. Retain the curved HD-CART-inspired ORIGINAL vector silhouette, graphite/blue palette, stereo PCM meters, two persistent hero knobs, and six parameter tabs; remove all nonfunctional marketing text.

- Single visible identity: `REVERB-SIS` in an understated 22pt font at x47,y65,width247,height32; no logo glyph or icon.
- Remove the source strings `BUILT-IN / 8 VOICE PRESETS`, `ARSONMIX / VOCAL REVERB`, `AR-VERB`, and the visual `PCM OK` label entirely, not merely painting over them.
- DSP PCM-applied ACK and pending edit state remain live; they update the preset selector tooltip only when state changes. No foreground thread status stamp and no native DSP behavioral/serialization/catalog ID alteration.
- Fix a measured layout defect: five 120-pixel-wide caption boxes had only 115-pixel pitch (5-pixel overlap). New captions are 108 px wide with **7 px clear gutters**, centered on the true knobs; y224 creates separation from tabs ending at y219.
- Rebalance top-right selector to x934,y78,width139,height28, distinctly above the Dry/Wet heading at y125. Preserve the central preset bay at y17.
- Real native JUCE test iterates **all six tabs** and asserts exactly 7 visible sliders and captions, all captions inside editor bounds, zero intersecting caption boxes, zero caption collisions with any tab, and persistent hero controls. The test also asserts exact brand and absence of the removed status label.
- All changes in one owned JUCE native editor, no new render loop, no duplicate WebView/PCM graph, no new DSP or latency, no source-owned branding assets copied.

Screenshots must be rendered again at 100% and 150% native Windows DPI, compared against both the previous Windows artifact and user-supplied HD-CART reference. Do not claim 100% pixel fidelity or installer readiness without full gate and physical ASIO FAT.
