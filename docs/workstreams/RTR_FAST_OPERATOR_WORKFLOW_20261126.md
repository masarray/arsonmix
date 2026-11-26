# RTR fast operator workflow — 26 Nov 2026

Baseline canonical #899 6970a54e203445330ff7754f40bd049e8be89e06; related #927, #837, #412, #192, #708. Scoped changes, not a release.
Owner requests: Apply Pins closes, direct Delete Track, close moved FX editor, clear output workflow, New Project / reset, smart inactive FX. 

## Delivered source slice
- Apply Pins closes only after accepted independent installed readback of the initiating pin transaction; new driver discovery and failed/stale/remap results retain recovery UI.
- Right-click audio Delete Track is immediate intent, native ACK/readback still required. MAIN remains protected, AUX/BUS confirmation retained.
- Close affected slot-relative built-in editors and exact moved/removed vendor instance before translated native graph mutation; keep unrelated channel editors. Conservative close on rejected execution after successful preflight is acceptable; user can reopen. PCM/FX instances are preserved by existing move contract.
- One Output shortcut routes to existing Additional Output panel (MAIN copy). Explicit endpoint, connect/arm/disconnect, OBS paired capture hint. Audio I/O Outputs links to it; no new native authority, no auto-arm. ASIO pins still use existing per-channel physical OUT.
- New Project shortcut exposes existing offline EMPTY transaction, archive intact. It is not yet a one-click live reset.
 
## Remaining critical implementation (not claimed done)
- Live New Project: capture exact current native document/state and verify durable archive BEFORE replacement, build fresh 10CH/4AUX/MAIN via existing logical workspace + verified MAIN binding, stop player/orbit, disarm independent output, replace native graph once, fresh session/IDs and persist new checkpoint atomically. Failures must retain previous project or offer recovery; no deleting HEAD, no forced device reset, no auto-arm. Existing offline transaction can reject retained graph; expose actual native readiness and add online path rather than sending an offline request when live. Verify failure injection, restart, hotkeys and FX/editor cleanup.
- dearVR: field report shows no source on Track13, MAIN route and 343595 process calls; zero snapshot peak does not prove no tail or autonomous generation. Do not add a blanket silence gate or special-case name. Build capability-based dormant route/FX admission: verified no physical/PLAYER/inbound source, no MIDI/sidechain/transport dependency, exact silence and drained tail (or explicitly qualified reset), same-block wake, PDC maintained, vendor/version evidence. Unknown third-party FX remain conservative; collect actual dearVR tail/metadata and A/B bypass/remove, attribution. Separate meters/analysis offload per #927. No crackle-fix claim.
- For output UX follow FL Studio's per-track direct physical OUT + parallel MAIN, Studio One's named output mapping; use named destination and verify current map, separate MAIN copy to other device. Independent per-bus cross-device route is not implied by existing OUTPUT1. Later unify setup and output routing in approved /preview/ without duplicate states.

Research: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm ; https://support.presonus.com/hc/en-us/articles/4411297438733-Revelator-io24-Livestream-with-Studio-One-on-Windows ; https://support.presonus.com/hc/en-us/articles/210041503-Studio-One-Creating-a-song-from-an-Empty-Template-Part-1 ; https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html . 

Validation: focused source contracts updated; actual Node/Windows/FAT not run by author. Keep canonical #899 untouched during qualification. Reconcile exact common-base hunks and run relevant tests before merge.

## Follow-up implementation
- Inline built-in editor ownership now includes GEQ and closes the affected source/destination racks before native MOVE/REMOVE intent.
- New Project now also has a live native path for admitted direct devices: stop/mute, capture current project, create and verify unique archive on the existing IO worker, recheck device/graph/roster and exact mixer/plugin state, restore the existing fresh logical workspace through native binding, request normal durable checkpoint. No auto-arm. A failed archive or changed state rejects replacement; old project remains. A checkpoint start failure after successful replacement is reported, with old archive preserved. Requires Windows build, UI and hardware qualification. No claim of tested live reset.
