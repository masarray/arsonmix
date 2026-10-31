# MS5 — Native-backed AUX Sends-on-Faders (operator milestone)

Date: 2026-10-31. Based on main@0de4f15d979e434cf09e12b38a4424d0b5e83464.
Related epic: #179 (selected CHANNEL / typed layers / Sends-on-Faders).

## Real operator flow
1. Choose a verified DIRECT ASIO/WASAPI device; patch the source channel to a real enabled input pin, output muted when applying the initial route.
2. In MIX → SENDS, use a channel's AUX editor to enable AUX1–4 return and create an initial POST tap. PRE remains a separate independent tap.
3. In the left rail select the intended AUX destination and click **SEND FADERS**. Each input strip's original MAIN fader is visually replaced with its existing **POST** send gain, or an explicit **+ POST** button for a missing edge. The physical MAIN/MASTER fader remains unchanged.
4. Drag an existing AUX send fader, or use keyboard Arrow/Page/Home/End. The visual feedback follows the gesture locally; **one** typed gain update is staged on pointer release (not per 12-Hz meter frame or mouse motion). Creating a missing POST tap uses the existing bounded native send controller, with truthful errors for unpatched channels, disabled returns and the 32-edge budget.
5. **DRAFT is not LIVE**. MUTE outputs → APPLY → wait for native positive ACK. Exit or switch FX/ROUTE mode without silently sending new main-fader updates. REVERT restores the accepted route.

## Signal and safety truth
- This is a **POST-only send-fader bank**, NOT a second PCM DSP graph, DCA, physical AUX output, monitor CUE/PFL bus, or DAW automation lane. Existing PRE taps are unchanged.
- The sole command handler is `window.__arMixStageAuxSendEdit({from,bus,tap:'POST',action:'gain',value})`, owned by `web/monitor.js`; one existing `routingDryApply` MUTE→native ACK controls publication.
- Gain is bounded 0…1 (−∞ to 0 dB); visual movement is scaled in dB and clipped at -60 dB for OFF. No persistent real-time parameter event on pointermove.
- Active destination and epoch are explicit. Device epoch change, unavailable native engine, or pending APPLY cancels/blocks the operation; no auto-unmute, plugin re-creation or fader-setting overwrite.
- Plugin rack, meter, 11-fader aligned geometry and pinned MASTER remain resident. The original per-channel MAIN fader DOM is hidden only in SEND FADERS mode and restored on exit. No blur, animations, RAF render loops, or DOM rebuild on drag.
- Hardware #15 crackle, real ASIO field latency, PDC, dynamic live +CHANNEL and FULL LV1 GROUP/DCA/MATRIX/CUE remain independent, **not solved by this UX feature**.

## Acceptance and regressions
- Node contract: one native draft controller, POST-only semantics, epoch validation, pointer release commit, no extra DSP.
- Chromium: accepted AUX1 gain=0.5 appears ~-6dB; keyboard End stages gain 1 but emits **no** `routingDryApply` until explicit APPLY; original MAIN fader value and DOM identity remain unchanged; EXIT restores main control and REVERT restores native gain.
- Browser and bundled standalone HTML must be source-byte equivalent via `python scripts/build_ui_css.py --check`. CSS is independent of global chrome and L2 geometry. Screenshot smoke at 1366/1440/1680, 11 faders aligned in FX/ROUTE/SENDS.
- This is ready for code/CI review, not physical live-sound certification. Windows package smoke and 0FX real-device FAT must still be performed before release.
