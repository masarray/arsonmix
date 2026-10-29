# R2 — native CH→AUX send matrix and inspector (2026-10-29)

## Delivered milestone

The integrated INTERNAL/SENDS tab now provides a compact **10 CH × 4 AUX** matrix (40 bounded, hover-inert cells), with a shared contextual inspector for **PRE and POST taps**, level and pan. One source/destination pair may carry both PRE and POST simultaneously. Selecting a cell **never** sends an audio command; a separate deliberate CREATE/EDIT action changes only the existing `draftSends` model. The same array is serialized for the current `routingDryApply` message with device epoch, route revision, output-MUTE preflight and authoritative `acceptedRoute` ACK. There is no second FX graph, independent audio callback, 12Hz matrix repaint, catalog rebuild or CSS blur.

Directed CH→CH sends remain available as an advanced compact list beneath the AUX matrix, sharing the existing **32-total-send limit**, same source indices and PRE/POST semantics. Local preflight checks disconnected sources, disabled AUX, nonfinite gain/pan, duplicate source/destination/tap, self-loops and CH routing cycles **before** invoking native APPLY. The existing native `NativeDryRouteCommand`, `PreparedRenderGraph` DAG compiler and revisioned publisher remain final safety authority. AUX returns remain 4 independent stereo returns to MAIN, never AUX→AUX or unsupported physical outputs.

Matrix cells distinguish OFF, DRAFT, LIVE verified by same-epoch accepted native route, and REMOVED until APPLY. Editor controls are built only when INTERNAL opens. The current 14 strip/AUX VST3 buttons keep the shared lazy single picker (40-result cap). Changes are event-driven with no per-frame meter or pointer work.

## Verification

Native JavaScript parse, deterministic `live_routing_ui_contract_test.mjs` source contract and Playwright Chromium flow verify lazy 40-cell grid, invalid unpatched source, PRE+POST pair, level edit without accidental APPLY and revert to accepted route. Exact-head CI must pass before merge. Windows MSVC/CTest and artifact checks remain a separate native gate; no physical ASIO/WASAPI hardware FAT is inferred.

## Unchanged limits

R0B #157 NORMAL fail-silent startup, B2 FX prepared graph handoff and vendor Orbit are untouched. No physical AUX hardware output patch, PDC, automatic input mapping/unmute or full FX-equipped Show recall is claimed. Physical zero-FX crackle #15 remains open until real hardware acceptance.
