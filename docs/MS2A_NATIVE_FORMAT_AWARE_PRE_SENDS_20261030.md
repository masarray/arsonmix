# MS2A — Native Format-Aware PRE Sends (2026-10-30)

**Status: integrated code change for review, not full MS2 or production qualification.**
Parent: [MS2 #174](https://github.com/masarray/arsonmix/issues/174). Base: main@928dfdfd793bf69398c0c62f8a585d436efc6333. Protect merged MS1 #184, G5B #186, L2 #176 and P0 #15.

## Delivered native slice

- The one existing `PreparedRenderGraph::compile` passes the already-validated `effectiveFormats` to both internal `ChannelSendPlan` and dedicated stereo `AuxBusPlan`.
- PRE-FADER sends from a MONO processing channel project duplicated mono L/R scratch through equal-power coefficients: center -3.0103 dB per side, hard-left/right unity, no extra physical right pin. Stereo channels retain L/R independence and existing balance (no width collapse).
- Coefficients are calculated only while building the immutable graph. The realtime callback dispatch loop is **unchanged**: identical bounded two multiply-accumulates per frame, no allocations, trig, locks or device work.
- Source-format arguments are backward-compatible for legacy standalone planners; a supplied but invalid/partial format set fails closed and leaves the previous destination plan untouched.
- Actual native PCM assertions cover both direct channel-to-channel PRE and stereo AUX PRE paths, fader=0, solo/mute closure, graph rollback, no callback allocation, mono hard-left and hard-right, stereo balance, and native v1/v2 command compatibility.

## Important honest limitations (remaining MS2)

POST taps currently contain the strip's existing fader/channel PAN. Applying a *second* equal-power mono pan without a separately owned mono post-fader pre-pan tap would double-attenuate center sends and cannot correctly re-pan a hard-left source to the right. **POST remains the previous balance law**, intentionally. The next MS2 implementation must add a bounded, single-owner mono post tap and prove gain/pan/solo ramps without double-processing or RT allocation before changing this law.

Typed MONO FX AUX/GROUP receivers, mono/stereo VST3 bus negotiation, PDC, real GROUP GUI and independently routed hardware MON outputs are **not** delivered by MS2A. Existing four stereo AUX / ten channels / 32-edge native bounds remain. Stale device epoch and OUTPUT MUTE/APPLY/ACK remain controlled by the existing native route service.

## Acceptance / FAT

Run original Windows MSVC CTest and Chromium UI smoke against exact PR HEAD and compare samples on real ASIO/WASAPI for two mono microphones into one shared stereo AUX reverb. #15 zero-FX crackle must be qualified separately before a stable/production label.
