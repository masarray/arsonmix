# Native WIRE gain and channel OUT

Continues PR #418's real channel-to-channel PCM and source-only recovery.
The approved `/preview/` layout and single JUCE audio callback remain the
product owners.

## Behavior

- WIRE send gain and Sends on Faders target an accepted native EdgeId.
  Drag updates coalesce while one intent waits for ACK and independent
  accepted readback. No optimistic audio state is presented as accepted.
- Gain is a scalar update: graph/roster revisions and strip DOM stay stable.
  The existing preallocated DSP gain slew spans 64 samples. No graph compile,
  allocation or new callback is introduced by a gain edit.
- Primary OUT stays at unity; use channel/destination faders for its level.
- OUT choices include INPUT, GROUP and FX_RETURN, resolved by ChannelId.
  Duplicate and reserved names get an identity suffix. OUT uses the same
  exclusive ROUTE_ONLY transaction as WIRE, including cycle validation.
- Source-only/dormant sends remember accepted gain. A subsequent hardware
  source preparation must restore that gain instead of reverting to unity.
- Device, graph or roster changes discard queued old-identity edits.

## Verification

`approved_native_routing_pcm_test.cpp` exercises public decoder/executor,
accepted session/publisher and actual rendered samples. It checks level,
64-sample smoothing, accepted readback, unchanged revisions, invalid/primary
edge rejection, dormant gain restoration and zero render allocations.

`approved_native_m2_contract.test.cjs` covers latest-value coalescing,
ACK/readback proof, destination/tap identity and ambiguous OUT names.
`approved_native_wire_gain_smoke.py` uses real compiled native HTML and
Chromium keyboard input at 1366x768, 1680x900 and 1920x1080. Its backend is
emulated for UI isolation; it is not physical audio evidence.

Physical ASIO/WASAPI, third-party VST3 listening and live stability remain
PENDING until measured on a real rig. No stable qualification is implied.
