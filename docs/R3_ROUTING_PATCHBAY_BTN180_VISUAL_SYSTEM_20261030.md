# R3 Routing Patchbay Visual System — BTN-180 unification (2026-10-30)

## Purpose
Bring ArSonMix ROUTING up to a coherent professional mixing-console visual hierarchy, using the repo's **approved BUTTON 180** material for all operator command pads, not inventing a new button face. Reference the **information architecture** of Waves eMotion LV1 Patch (strong row/column matrix, stable patch states, distinct INPUT/INTERNAL/OUTPUT views) rather than copying its assets or claiming unsupported routing operations.

## Changes (front-end only)
- ROUTING header: clean product/patchbay title, concise current physical device identity and separate **OUTPUT MUTED / OUTPUT ARMED** status chip. State derives from the existing native `state.monitor` and updates only when status changes; no extra timer or pointer handler.
- Every button that represents a **command or mode** uses shared `.btn-180`: tab views, page arrows, close/Google Material X SVG, SAVE/BROWSE/RECALL Show, APPLY/CLEAR/REVERT, L/R side selectors, shared FX selector, AUX tap create/remove and advanced CH-send remove. Material lives in `web/styles-src/20-button-180.css` bundled into `web/styles.css`; ROUTING defines only semantic palette variables and size. This prevents a second shadow/press algorithm.
- **Patch cells remain flat**: they convey OFF/LIVE/DRAFT/REMOVED states, not physical keys. Semantic colors survive last ACK and are never labeled LIVE by local click before native publication. No CSS hover filter, blur, backdrop-filter, overlay glow, animation or pointer-move state.
- Larger, clearer patch grid headers, more legible zebra-like surfaces and full available panel width; AUX PRE/POST matrix no longer unnecessarily capped at 760px. Keep bounded 10 INPUT × 4 AUX and 8 hardware columns/page; no growing DOM catalogs.
- Footer remains a persistent command band, responsive wrapping on standard Windows displays, with safe positive green APPLY and destructive pale red CLEAR using only `--b180-* ` variables. BROWSE Show area is ordered immediately above footer, without changing control IDs or event listeners.
- Font: existing embedded `ArSonMix Inter` from `web/styles.css`. No external fonts or assets. Single static style insertion; no extra canvas or raf.

## Hardware & application safety
No changes to native audio callback, VST3 graph, routing transaction model, epoch, device source of truth, PRE/POST behavior, master state, or portable Show store. MUTE remains mandatory for APPLY/RECALL and no auto-unmute. OUTPUT patch still explicitly READ-ONLY; unlike Waves eMotion LV1, ArSonMix does NOT expose physical AUX-to-output routing, groups, network device patching or 4 different output tap sources.

## Verification
- JS parse and source contracts: 10 declared operator buttons, tab/pager/side/tap buttons class binding, semantic palette, single imported BTN-180 recipe, status derivation, zero blur filters.
- Chromium UI smoke: verify actual computed button material, disabled/positive semantics, 10x8 grid, no eager plugin catalog generation, 40 AUX matrix cells, native ACK/DRAFT, and footer bounds at desktop. No shadow transition on hover.
- Build CI only after Chromium/head passes, and merge only when validated. Windows Native MSVC/package does not certify slow GPU performance or physical crackle (#15).
