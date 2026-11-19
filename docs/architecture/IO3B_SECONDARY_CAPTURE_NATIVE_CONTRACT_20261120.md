# IO-3B — Typed secondary ASIO capture control (native-only integration contract)

**Scope:** approved ArSonMix `/preview/` **single** v8 console request/ACK/readback transport; Windows JUCE primary ASIO A plus one independently-clocked input-only ASIO B. **Not yet an operator-facing GUI or physical FAT result.** Cross-workstream source: [multi-device ARD](../ARD_ARSONMIX_MULTI_DEVICE_IO_V2_20261118.md), [coordination #812](https://github.com/masarray/arsonmix/issues/812).

## Native command (implemented source boundary, not enabled in published picker)

```json
{
  "operation":"INSTALL_SECONDARY_CAPTURE",
  "requestId":"<one existing v8 session requestId>",
  "deviceEpoch":"91",
  "baseGraphRevision":"25",
  "baseRosterRevision":"9",
  "targetChannelId":"9007199254740993",
  "payload":{
    "confirmed":true,
    "secondaryDriver":"<exact selected verified ASIO capture driver name>",
    "secondaryInputPins":[0,7],
    "captureRate":44100,
    "capturePeriod":128,
    "sources":[
      {"channelId":"9007199254740993","deviceRole":"SECONDARY",
       "leftPhysical":7,"rightPhysical":-1,"stereo":false}
    ]
  }
}
```

All IDs are **canonical uint64 decimal strings**; do not round through JavaScript Number. `targetChannelId` must be an installed INPUT that is actually assigned to B in the `sources` list. Every source row must refer to an exact installed ChannelId and a selected, observed native **physical** input index (not a dense JUCE callback ordinal). `secondaryInputPins` must be unique, sorted, 1–16 channels and within supported driver bit bounds. `PRIMARY` input rows, when needed to preserve currently accepted A channels, use the accepted A physical mask. Incomplete A/B rows are rejected by the native qualified graph compiler.

The native CONTROL decoder `integration/QualifiedCaptureConsoleIntent.h` checks the exact installed public graph, matching internal nativeBus revision/roster, operator confirmation, requestId and input roles. The one `Main.cpp::handleApprovedNativeControl` dispatcher invokes `LiveAudioEngine::installSecondaryAsioCapture`, which **rechecks** actual ASIO driver identity, registration class, current A/B pins, negotiated sample rate/buffer and native leases before publishing one ChannelId PCM/FX owner. Outputs must already be disarmed; **no speaker is auto-armed**.

## ACK and later readback

The existing `arMixConsoleAck` replies with requestId, `status` ACCEPTED/REJECTED, errorCode and installed deviceEpoch/graphRevision/rosterRevision. Native controller also includes `secondaryCapture` diagnostics (driver, physical mask, generation, lease, clock and fault) on this operation. ACK acceptance requires verified installed B capture readback, not merely driver `open()` success, and `productionCallbackLive` / `sourceAudibleProven` remain **false**. The approved consumer must request a *later* `arMixConsoleSnapshot` and verify the same B identity, mask, epoch, lease, ChannelId map and advanced graph revision. `WAITING_FOR_PCM` and `PCM_FLOW_OBSERVED` are native diagnostics, **not proof that the assigned channel is audible**; priming/underflow frames do not count as B PCM.

**Important integration gap:** The current browser `web/approved-native-contract.js::readbackProvesIntent` does not yet recognize `INSTALL_SECONDARY_CAPTURE` and the published Source Picker does not expose this operation. **Do not advertise it as usable or activate via an unreviewed JS side path.** Follow-up IO-3C must implement the approved controller capability + exact later readback with canonical Pages generated assets and Chromium tests in one vertical slice. Do not add a second WebView event bus or standalone preview.

## Persistence, fallback and hardware qualification

Current v1 `.smix` cannot store both physical driver identities/pins. While a qualified A+B graph is installed, Save/autosave is intentionally **rejected before replacing the last known-good file**. Explicit loading of a saved primary-only `.smix` now uses `QualifiedMapChange::replaceWithPrimaryOnly` under a fully muted native transaction, returning to direct A PCM and retiring the B capture reader **only after** successful publication; WIRE/FX-only edits continue inheriting the exact qualified B map. This is a safe temporary guard, not an IO-5 persistence implementation.

Real simultaneous two-vendor ASIO input PCM, CLI driver open constraints, Windows installer, latency and hardware FAT remain **NOT RUN**. The synthetic native fixtures prove graph/ASRC policy, not hardware compatibility or actual acoustic performance. Independently routed BUS/CH Device B output remains a future scope, not the existing WASAPI MAIN mirror.

## Required follow-up

1. Parent IO-2D #825 exact-head Windows MSVC/CTest + Codex + protected Required CI Gate, then merge.
2. Reconcile stacked #830 with *latest main*, run its own exact-head Windows and approved GUI gates.
3. IO-3C actual compact Device B picker and later accepted readback in existing approved `/preview/` only; regenerate canonical embedded GUI with scripts and test compiled resources.
4. IO-5 durable physical binding schema and offline remap before enabling A+B Save; physical two-vendor ASIO FAT before shipping product capability.
