# ArSonMix P1 — FX Rack and Routing ownership contract (2026-10-30)

## Why this exists
A recent Routing release replaced every strip's six visible FX insert rows with one `+ ROUTE / FX` button while `dryRoutingLab.active`. That removed the working FX workflow from the user's mixer. The FX editor must remain independent from the physical routing patchbay.

## Mandatory user-facing invariants
1. **MIX** always retains the six-row FX rack geometry on every input strip, with real accessible actions only for implemented native inserts. Never substitute a Routing button for the FX rack.
2. **FX RACK** is an independent workspace to browse, insert, replace, bypass, inspect and manage VST3. **ROUTING** is a separate workspace for physical input/outputs, sends and patch safety.
3. Existing **CH1 multi-insert/Orbit** is a live native graph. `src/LiveAudioEngine.h` / existing `addCatalog`, `fxMove`, `fxRemove`, `fxBypass` commands remain the authority; never replace the CH1 8-slot FX Rack just because the prepared 10CH graph is active.
4. The current native routed 10CH/4AUX engine supports **one routed effect per channel/AUX**. Do not render five extra active fake ADD controls or claim the native FX is actually loaded before ACK. Additional multi-insert support requires a future engine milestone and a realistic PDC/per-strip lease plan.
5. Routed FX selected from FX RACK becomes **DRAFT** and follows the existing `routingDryApply` contract with strict device epoch/revision and explicit output-MUTE gate. `arMixRoutingLabAck` makes it LIVE; rejection leaves the live graph unchanged. No new audio callback, no implicit unmute.
6. `PluginCatalog` is the persistent source of truth for favorites, stored in Studio's portable `plugin-catalog.json`. A rescan preserves each variant's favorite flag; the UI sorts favorites first and provides favorites-only filters.
7. Main window cold startup is concealed by the opaque native splash; WebView2 uses JUCE keep-page-loaded and dark underlay. Avoid `goToURL`/WebView reload on minimize/restore, and don't add per-frame or pointermove compositor work.
8. Prefer 30–32px operator click targets, compact readable typography, native Windows file dialogs, no blur-based glow, no fake STATUS=OK claims before plugin instantiation.

## Responsibilities to avoid cross-thread regressions
- **Routing thread:** edit `web/monitor.js` only for the patch workspace and the shared `__arMixStageRackFx` / `__arMixApplyRackFx` draft-to-ACK interface. Never alter `web/bridge.js` FX Rack ownership, remove the six insert rows, or shadow the CH1 FX/Orbit lifecycle.
- **FX/Plugin thread:** own `web/plugin-ui.js`, `web/plugin-ui.css`, `src/PluginCatalog.h`, and the FX Rack presenter in `web/bridge.js`. Reuse native Routing API for routed channel FX, never implement a second routing graph.
- **Native audio thread:** future per-CH multi-insert support belongs in `LiveChannelFxGraph` / prepared graph handoff only after ARD, explicit CPU/PDC/RT/lease design and hardware FAT. No RT allocation/lock/plugin creation.
- **Startup/GUI thread:** own WebView2 lifecycle, splash, restore testing on low-end Windows PCs, and GUI geometry.

## Acceptance gates
- One aggregated feature PR against fresh main; existing Windows MSVC+CTest/package verifier and Chromium smoke only, no extra CI matrix.
- Chromium checks 241+ catalog items, favorites, large star/button hit targets, stable picker input/caret, FX Rack six rows in native 10CH session, routed ADD as draft, fail-closed APPLY without physical patch.
- Native build must pass before merge. Only verified **main branch** Windows artifact should be given for real hardware testing.
- Real ASIO/WASAPI crackle FAT #15 remains unqualified; CI is not audio hardware validation.

## Future major milestone
Expand native routed CH2–10 and AUX from 1 to a configurable multi-insert graph with independent ownership, PDC, atomic change transactions, stateful Show save/restore and common FX Rack API. Do this separately, not by hiding VST racks or adding fake ADD buttons.
