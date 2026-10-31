# DEVICE4 — Studio-style named software input ↔ real hardware matrix (2026-10-31)

Built on DEVICE3 #239: native ASIO / Windows Audio toggle, real device and port enumeration, OUTPUT MAIN/MONITOR and the existing MONO/STEREO channel patch controller.

## Operator flow
1. AUDIO SETUP → DEVICE / DRIVER → installed ASIO/Windows Audio → device → APPLY DEVICE (muted), then wait for JUCE ACK.
2. INPUTS → Add Mono or Add Stereo → name the logical input → click actual M or L/R driver pins → SAVE I/O.
3. Saved names appear in the already-existing channel SOURCE chooser. The row action → CHnn uses the very same SOURCE2 native source-switch controller, with pending/ACK/mute handling. Direct CH01–10 crosspoints remain in an expandable section.
4. Edit or remove a software name without silently rewriting the accepted native channel route. OUTPUTS still maps actual supported MAIN/MONITOR; arbitrary output buses are not added.

## Safety
Names are SOURCE2 device-scoped aliases (backend, capture endpoint, full enabled input-pin/name signature). They are not new audio streams, channels or DSP engines. Changing hardware epoch clears unfinished editing state. Aliases are invalid when a changed driver cannot attest its pins. Mono needs one verified pin; stereo requires two distinct verified pins. Incomplete edits are never saved. Saving metadata does not change live routing. Only positive native ACK can change channel sound.

## Gates
Node operator tests, Chromium UI and Windows native CTest must pass on PR head before merge. Physical Windows ASIO/WASAPI audible FAT and issue #15 zero-FX crackle remain unverified. No measured zero-latency claim.
