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
