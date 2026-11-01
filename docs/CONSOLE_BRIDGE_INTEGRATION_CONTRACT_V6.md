# Console V6 native ↔ UI integration contract (control-thread only)

This document describes **implemented integration types and executable PCM/readback tests** on a separate INTEGRATION branch. It is NOT a new DSP graph and **NOT wired into the production JUCE callback yet**. The ENGINE PR #266 owner must perform the full-fidelity callback cutover; GUI issue #268 must consume *native accepted*, not fixture-only, snapshots.

## Authoritative sources
- Main accepted graph: \`DynamicAcceptedSession\` and its \`DynamicNativeSessionPublisher\`; role and routing projection: \`DynamicConsoleRoutingView\`.
- \`integration/ConsoleNativeContractV6.h\` is a **pure control-plane readback/preflight adapter**, not a second state owner. It cannot render sound and never determines real device pin mapping.
- \`tests/console_native_integration_test.cpp\` renders actual native offline PCM for IN1→CH7, IN2→CH2, sends to MONITOR, Group Selected with exclusive primary OUT, parallel wet FX Return, stale-device/invalid-pin rollback and mute. The test refuses to call this offline graph physically LIVE.

## Fields (target UI serialization)
**Wire synchronization v6 (integration patch):** `ChannelId` and destination IDs are canonical unsigned **decimal strings**, never JS Number; `requestId` is a bounded printable **opaque string** per WebView session (e.g. `gui-live:41`) echoed unchanged by ACK, not a native integer or a global monotonically increasing counter. `device.epoch`, `graph.revision` and roster revisions are JS-safe integers on the *current* GUI branch; native must reject values beyond 2^53-1 until both ends migrate together. Only v6 represents these roles. Sample:
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

Intent envelope: `{requestId,deviceEpoch,baseRevision,baseRosterRevision,op,targetChannelId,payload}` as currently emitted by GUI #271. Native maps `baseRevision` to the internal `baseGraphRevision`, checks exact device epoch and accepted revisions, canonical decimal ChannelIds, and validates the operation against **runtime native supportedOps**, not the UI fixture. A single native writer maintains a **bounded recent completed-ID cache** (not an arbitrary numerical sequence comparator) across editor reconnects and rejects duplicate IDs, stale epoch/revision and unsupported operations before dispatching an existing accepted native graph edit. This adapter is control-plane *preflight* and does not yet instantiate that native writer. ACK reports the same requestId, accepted/rejected, **current accepted** revisions and precise reason. An accepted graph **does not** imply physically audible LIVE: a full-fidelity production callback plus verified device/output gate is needed.

## Invariants and merge gate
1. On rejected native intent, the accepted graph revision, source bindings and PCM remain unchanged. Never send an optimistic UI 'LIVE'.
2. Group Selected atomically replaces the selected channels' primary OUT without creating two dry paths. FX Return is a separate wet send, not the Group bus. Monitor remains independently rendered.
3. Fully admitted native capabilities remain **false** until actual M4→ChannelId callback switch, external CH1/Orbit VST lease, Show and verified output pin path are exercised. These test-only flags are deliberately false.
4. CTest/Chromium PASS ≠ real hardware clean sound. Hardware #15 must verify ASIO/WASAPI 0FX/1FX on a physical device with actual pin masks.
5. ENGINE owns adding source/FX readback from the native accepted snapshot in #266. GUI owns serialization/rendering in #268. Integration test owner does not edit those files.

**Scope note:** This is one integration prerequisite, not the full Universal Live Console milestone. Do not merge this test as a claim that CH11, Group or FX Return have shipped.

## Wire identity / duplicate ACK regression (2026-11-01)
The integration C++ test now proves an opaque `requestId` is echoed unchanged in both accepted and rejected replies, previously completed requests are rejected even across multiple editor IDs, a new editor restarting from sequence 1 is permitted, invalid control characters/overlong tokens are rejected, and native uint64 ChannelId strings reject overflow, leading zero and UI-fixture aliases. Snapshot projection refuses a JS-unsafe epoch/revision. The actual native WebView command dispatcher in ENGINE #266 must implement this v6 mapping and ACK/replay; the existence of the C++ test is not evidence of production bridge or physical sound.
