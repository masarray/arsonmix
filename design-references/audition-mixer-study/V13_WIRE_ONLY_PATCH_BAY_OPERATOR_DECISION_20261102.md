# V13 — WIRE-ONLY Patch Bay: Remove BUS Dropdown and SEND Slider

Date: 2026-11-02. Source of truth: owner-approved isolated GUI PR #278.

## FL Studio reference and operator decision

Image-Line's official Mixer routing docs show **select source → destination up-arrow switch → per-route send knob**, green wire/arrow indication, and independent direct-Master enable. A separate "choose BUS" dropdown + second horizontal Send Level slider in **every** main channel strip adds no value to that primary routing workflow. Accordingly:

- **Remove from main channel DOM**, not merely hide with `display:none`: `S1–S16` selector, BUS/FX destination dropdown, legacy Pre/Post and Power buttons, horizontal Send Level input/output.
- **Retain per-link gain** as the existing embedded physical rotary pot `wire-gain`, range −60…+6 dB (0 dB unity). Do NOT remove gain from routing or conflate it with the source fader.
- Main control is a **full-width WIRE PATCH BAY**, in the *existing middle SEND/BUS row*, below FX rack, above PAN and M/S/PFL/fader. Compact 80 px, Expanded 160 px via the existing rail button; compact short-display fallback 58 px at <=680 px viewport height.
- A wire has a visible source ▼, available destination △, connected ◆, and one self-contained knob. Hover cable is dashed amber; committed GUI demo cable green (send) or cyan (pre-FX reference), arcing upwards through the recessed empty patch canvas.
- **Silence unconnected channel wells** visually: do not repeat “CHANNEL PATCH” on ten empty strips. Only source and active receiver display tiny relevant metadata. Pinned Master displays direct connection state.
- Keep the newest geometric P0 contract: FX rack owns **all reclaimed space** from ROUTE and WIRE; PAN, MUTE/SOLO/PFL, fader, and footer retain the same Y positions and heights for all eight ROUTE/FX/WIRE permutations. The approved viewport-proportional fader height is `clamp(150px,100dvh - 630px,485px)` and does **not** shrink or jump when opening the 160px wire bay. The console body faces remain flat anodized metal and FX is the elastic upper section.

## Compatibility / native engineering boundary

Source data retains 16 legacy send slots, active send state, snapshot compatibility, and old handlers for migration; they are **not** duplicated as a second set of per-channel operator controls. Existing legacy sends remain logically distinct from new wire edges. Keep guarding against duplicate connections into the same Bus/FX Return and display a LEGACY send cue on a connected destination when applicable. A follow-up native migration must reconcile legacy sends into a single canonical graph and expose advanced editing outside the main strip. Do not delete saved routes or silently change live sound.

**Crucial**: This is **GUI reference only**. No ASIO PCM fanout, native graph revision/ACK, VST3 or live wire audio is implemented here. A visible green line represents only accepted browser state. Real native implementation requires accepted stable IDs, validated graph, safe PRE INSERT/POST FADER tap semantics and device epoch, buffer-boundary publication, PDC/headroom and live FAT.

## Source verification and release gate

- V13 smoke must confirm **no** `send-select`, `send-slot`, `sendlevel`, `data-field=sendLevel`, `data-action=sendOn` or `data-action=pre` in channel-strip HTML.
- CH1 → CH3–CH6 by four arrows or click CH3 then Shift-click CH6 must preserve independent 0 dB physical knob on each destination.
- On real Chromium at 1366×768 / 1666×888 / 1920×1080: verify 80/160px wire heights, rail/channel/Master alignment, 4 FX rack insert rows, no clipped route/patch/header, no cable crossing FX/PAN/fader, and no horizontal page scrolling. Two screenshots should be reviewed by operator: compact and expanded patch.
- Public preview must only be updated after the final branch smoke and main Chromium CI validates the exact source snapshot. Keep PR Draft pending owner visual approval.

Sources:
- [FL Studio Mixer Audio Routing](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm)
- [FL Studio Mixer Explained](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm)
