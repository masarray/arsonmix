# P1: retained console focus and idle-only UI persistence — 2026-10-30

Baseline: `main@2c2bdf67aa7c93d11cea0dd63bd5ed8402dfaf97`. Source milestone for GUI responsiveness on standard Windows 11/iGPU. DYN0 native stable channel roster (#193) is merged but live dynamic audio/+CHANNEL still NOT wired. G5E native Show work #197 remains separate.

## Root causes and production correction
1. Sidebar and FX-workspace selection recreated ALL eleven mixer strip nodes through `renderMixer()`, invalidating the meter DOM cache and native rack anchor despite changing only which channel was selected. Now `chooseChannel` delegates to existing lightweight `focusChannel`, retaining rack/meter/fader DOM and redrawing only the selected FX workspace when visible. Full render remains reserved for structural edits such as actual FX chain edits, renames and snapshot recalls.
2. Browser-only UI workspace serialized the entire envelope and synchronously wrote localStorage every five seconds even with no edits. Now UI control/FX parameter edits mark a cheap dirty bit (including gestures while fader moves); release/click/keyboard completion schedules a 1.8s idle save with an 8.5s maximum deferral. Hidden/pagehide explicitly flushes. No idle polling, no new audio callback, timer per fader pixel or DSP change. The original one-key atomic schema-v2, two-megabyte bound and rejected-write rollback remain intact; failed writes leave dirty state.
3. Existing L2 #176 seven-row aligned strip, fixed 7px LED/meter behavior, approved BTN180 and fader silver/gold PNG, routing MUTED/APPLY/ACK and native plugin instances remain unchanged. Build script generates native/standalone from shared source.

## Qualification gates
- Chromium Playwright: click sidebar, assert all 11 strip node object identities and CH1 meter/rack DOM survived, selected CH8 visual + rail class then return to CH1; change gain and confirm atomic UI-only saved envelope after release.
- Node contracts guard against old full render/poll; existing Chromium mixer/FX/routing/snapshot, Firefox and Windows native tests gate merge.
- User hardware input-to-paint p95/p99 and real ASIO/WASAPI no-FX crackle (#15) still NOT certified. This code does not claim Studio One/eMotion feature parity or live DYN1/DYN2 channel scalability.
