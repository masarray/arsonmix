# Console V6 native ↔ UI integration contract (control-thread only)

This document describes **implemented integration types and executable PCM/readback tests** on a separate INTEGRATION branch. It is NOT a new DSP graph and **NOT wired into the production JUCE callback yet**. The ENGINE PR #266 owner must perform the full-fidelity callback cutover; GUI issue #268 must consume *native accepted*, not fixture-only, snapshots.

## Authoritative sources
- Main accepted graph: \`DynamicAcceptedSession\` and its \`DynamicNativeSessionPublisher\`; role and routing projection: \`DynamicConsoleRoutingView\`.
- \`integration/ConsoleNativeContractV6.h\` is a **pure control-plane readback/preflight adapter**, not a second state owner. It cannot render sound and never determines real device pin mapping.
- \`tests/console_native_integration_test.cpp\` renders actual native offline PCM for IN1→CH7, IN2→CH2, sends to MONITOR, Group Selected with exclusive primary OUT, parallel wet FX Return, stale-device/invalid-pin rollback and mute. The test refuses to call this offline graph physically LIVE.

## Fields (target UI serialization)
All \`ChannelId\`, destination IDs and request IDs are decimal **strings** in JS (64-bit native identity must not be downcast to Number). Only version 6 represents these roles. Sample:
\`\`\`json
{
  "schemaVersion": 6,
  "deviceEpoch": "69",
  "graphRevision": "4",
  "rosterRevision": "4",
  "productionCallbackLive": false,
  "capabilities": {
    "callbackOwnsChannelIds": false,
    "fullVendorFxMigration": false,
    "showAndOutputParity": false,
    "physicalCaptureMapped": false,
    "addChannel": false,
    "group": false,
    "fxReturn": false,
    "monitor": false
  },
  "channels": [
    {"id":"7", "role":"input", "processingFormat":"mono",
     "primaryOutId":"13","sourcePinVerified":false,
     "actualPcmPresent":false,"sends":[]}
  ]
}
\`\`\`
The snippet is **illustrative**, not evidence that physical INPUT 1 is bound to ChannelId 7. A device callback pin number is not a sparse Windows hardware pin ID. The native source/physical-output owner must supply separate verified \`sourceBinding\` and actual \`OUTPUT ARMED\` readback. No fallback to IN1 or mock data if missing.

Intent envelope: \`{requestId,deviceEpoch,baseGraphRevision,baseRosterRevision,operation,targetChannelId}\`. A single native writer should reject stale epoch/revision, a previously completed duplicate ID and unsupported operations, then dispatch an existing native graph edit. ACK reports the same requestId, accepted/rejected, **current accepted** revisions and precise reason. An accepted graph **does not** imply physically audible LIVE: a full-fidelity production callback plus verified device/output gate is needed.

## Invariants and merge gate
1. On rejected native intent, the accepted graph revision, source bindings and PCM remain unchanged. Never send an optimistic UI 'LIVE'.
2. Group Selected atomically replaces the selected channels' primary OUT without creating two dry paths. FX Return is a separate wet send, not the Group bus. Monitor remains independently rendered.
3. Fully admitted native capabilities remain **false** until actual M4→ChannelId callback switch, external CH1/Orbit VST lease, Show and verified output pin path are exercised. These test-only flags are deliberately false.
4. CTest/Chromium PASS ≠ real hardware clean sound. Hardware #15 must verify ASIO/WASAPI 0FX/1FX on a physical device with actual pin masks.
5. ENGINE owns adding source/FX readback from the native accepted snapshot in #266. GUI owns serialization/rendering in #268. Integration test owner does not edit those files.

**Scope note:** This is one integration prerequisite, not the full Universal Live Console milestone. Do not merge this test as a claim that CH11, Group or FX Return have shipped.
