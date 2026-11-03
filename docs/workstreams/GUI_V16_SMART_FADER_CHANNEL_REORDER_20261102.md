# ArSonMix V16 — Operator-smart Fader Space + Channel Arrangement

Source of truth: PR #278 `design-references/audition-mixer-study/index.html`; published at `/preview/mixer.html` and `/preview-v2/`.

## Layout acceptance
When FX and SEND are both folded, the operator has explicitly hidden all time-consuming upper processing controls. The fader immediately consumes the unused upper space, even when ROUTE remains expanded. PAN and M/S/PFL move upward coherently; pinned footer stays fixed. When FX or SEND is expanded, the fader is fixed and the upper panel claims its space. Eight collapse-state combinations including MAIN and left rail are verified at four window sizes.

## Front physical seams
A narrow absolute-positioned pseudo-element on the right edge of each channel paints the thin channel boundary above compartment backgrounds. Pointer-events none and no z overlay for the wider surface. No bulky highlights or gradient bands.

## Channel drag arrangement
Drag ONLY the nameplate/number header to rearrange channels within the visible bank, using drop side to decide before/after placement. Use Alt+Left and Alt+Right while header focused as keyboard access and to reorder across bank boundaries. `tracks[]` order changes; stable `id`, FX chain, send graph IDs, physical I/O mappings, fader values and pinned MAIN do not. Double-click/F2 still edit identity. No new fake PCM route or native engine ACK. Playwright asserts drag right/left, keyboard, routing immutability and pinned MAIN.

## Follow-on
Cross-bank mouse drag with hover-to-scroll, persisted arrangement/scene recall, and native WinWebView mapping need separate acceptance; this browser preview is still a simulation.
