# PATCH1 — Canonical audio-input to mixer-channel routing

31 October 2026 · baseline main 71bcd715 · source to channel workflow, no second engine.

> **HISTORICAL DELIVERY — PATCH1 #218, IMPLEMENTED, NOT FINAL UX CONTRACT.** Its per-row STAGE → manual MUTE → APPLY procedure remains the *current safety behavior* only. After the formal eMotion LV1 / Fender Studio Pro benchmark on 2026-10-31, the **future operator workflow** is defined in [PATCH2 PRD](PRD_HARDWARE_AUDIO_IO_MATRIX_20261030.md) and [PATCH2 ARD](ARD_HARDWARE_AUDIO_IO_MATRIX_20261030.md): configure named logical sources once, then choose an existing source directly on the channel. Do not remove current safety gates until native safe switching and physical FAT are proven.

## Operator workflow

- AUDIO I/O → INPUT ROUTING → CH03 → MONO → IN 1 → STAGE. Then MUTE output → APPLY PATCH → wait for NATIVE ACK. Live audio never changes from selecting a draft.
- MAONO PS22: install/configure the correct MAONO Routing Center/ASIO driver, enable the virtual capture channels, select the device through DEVICE / DRIVER. CH06 → STEREO → select the driver-reported virtual L and R pins (nonadjacent supported) → STAGE → muted APPLY → positive ACK.
- When the chosen ASIO/WASAPI device does NOT expose the requested virtual channel among its capture pins, do NOT invent one. Separate Windows capture endpoints cannot be combined by this mixer until a separate native multi-device clock-safe capture feature exists.

## The real implementation

- UI is a single, compact 10-channel source map with source type, actual L/R pin selectors and STAGE. It is the front-end for the pre-existing monitor.js __arMixStageChannelInput route draft; no new PCM graph, worker, listener per mouse movement or VST3 instance.
- M/L/R crosspoint is behind ADVANCED CROSSPOINT, but writes to the SAME native draft; top PATCH is explicitly Advanced Bus Routing, sends, FX and additional output with a direct link to AUDIO I/O.
- Legacy duplicate input dropdown and big mock hardware product illustration removed. Stereo L and R may be any two DISTINCT enabled pins reported by the currently selected device.
- Output safety: native MUTE -> APPLY -> revision/epoch ACK, reject stale or invalid assignments; no auto-unmute and no LIVE badge until acceptance. Distinguish source format from processing channel width.

## Acceptance

- Playwright native mock with sparse driver ports 0,1,4,7 and MAONO virtual labels tests physical INPUT1 → CH3 MONO, virtual ports 4+7 → CH6 STEREO; one routingDryApply payload and native ACK marks LIVE. This is mock-driver acceptance, not a claim a real PS22 is connected.
- Preserve independent MAIN/MONITOR output matrix, old AUX sends and VST3 FX, 11 L2 strip geometries, RAIL3 fader visual, JS/HTML source parity and no CSS blur.
- Real PS22 + Windows ASIO/WASAPI, endpoint/clock enumerations, audible source/return, feedback prevention, low-end iGPU and 0FX issue #15 must be tested physically.

## Operator design reference

https://faq.maono.com/docs/PS22-FAQs
https://faq.maono.com/docs/ps22_live
https://www.soundonsound.com/techniques/studio-one-audio-io-routing