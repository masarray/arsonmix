# R1 — Production-style native input patchbay (2026-10-29)

**Reconciled base:** `main@369c2a9c1fbf827815afb3f4866672ee47f554bf`, including approved GPU hover-inert optimization; merged R0A #144. **R0B dual-mode engine lifecycle is not complete.**

## Implemented

- Native ROUTING opens from toolbar **in one click** (existing SIGNAL FLOW action); no extra diagram or operator-facing START LAB. Original ArSonMix metallic charcoal / aquatic green UI, 1100px bounded panel, horizontal paging of eight fixed-size matrix columns per page (unused hardware input slots are disabled, never stretched into oversized tiles), 10 fixed CH rows.
- Matrix gets real physical source list from `dryRoutingLab.ports` on the active JUCE DIRECT device, not static assumptions or browser localStorage. Escaped device names; malformed, duplicate or invalid physical port descriptors ignored.
- MONO: click a physical input to patch; click again to clear; new choice replaces old. STEREO: independent visible L/R selectors; incomplete, duplicate-side or stale hardware port rejects before native call. NONE and BUS remain explicit. Physical fan-out to several channels is allowed by existing native routing plan.
- User edits are DRAFT only (amber). LIVE/green only if the device-epoch-matched acknowledged `acceptedRoute` reports that exact physical assignment. A removed cell displays old-live-until-APPLY status. REVERT reinstates native ACK, not a browser snapshot. Native `routingDryApply` and exact `arMixRoutingLabAck` are reused; no backend duplication. No automatic unmute; output must be MUTED.
- Input matrix redraw only on edit/tab/page, native ACK, device epoch/hotplug, and open. Delegated click/change and hover handlers, no per-cell listeners, no blur, no filter, no animation or audio-thread changes. Hardware status/peaks continue from existing 12Hz telemetry without full-grid repaint.
- INTERNAL/SENDS tab preserves existing CH/AUX/VST3 PRE/POST editor for R2 full internal matrix; OUTPUT tab exposes actual MAIN L/R device output mapping as read-only and accurately describes R3 as incomplete. No invented group/DCA/multi-output feature.
- Existing backend `dryRoutingLab`, `routingDryApply` and old JS alias remain for compatibility while R0B migrates CH1 FX ADD/REMOVE/MOVE/Orbit to the one default live native topology. UI no longer presents LAB as a product feature; **does not mean R0B is done**.

## Verification and limits

Native-mock Playwright exercises ASIO sparse port 2/5, escaped label, direct top nav, status DRAFT/LIVE/REVERT, stereo L/R duplicate prevention, exact epoch/revision APPLY, pending ACK, output read-only, device epoch 77→78 invalidation. Previous native/JS smoke tests preserved. Windows MSVC/CTest and native physical hardware FAT are separate; P0 #15 no-FX crackle remains open.
