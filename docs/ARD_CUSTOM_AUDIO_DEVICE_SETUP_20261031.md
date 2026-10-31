# ARD — Custom WebView2 Audio Device Setup backed by the existing JUCE manager

**Revision:** 2026-10-31 · **Status:** DESIGN / NOT YET IMPLEMENTED  
**Companion:** [PRD](PRD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md) · [handoff](HANDOFF_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md) · [PATCH2 ARD](ARD_HARDWARE_AUDIO_IO_MATRIX_20261030.md) · [product truth](PRODUCT_TRUTH_CROSS_THREAD_20261031.md).

## A. Current source reality and change boundary

| Existing owner | Current behavior | Planned change |
| --- | --- | --- |
| `web/mixer-io.js` | One overlay, 3 tabs, driver *name inventory*; `SELECT AUDIO DRIVER / DEVICE` still calls `audioSetup` | Replace DEVICE body with full working custom controls, use same overlay and INPUT/OUTPUT matrices |
| `src/Main.cpp` | `audioDeviceInventory` calls `scanForDevices` on JUCE message thread; `audioSetup` opens `AudioSetupWindow` and `AudioDeviceSelectorComponent` (64 pins after #224) | Introduce typed inventory/capability/apply/readback commands; normal UI never needs `audioSetup` |
| `src/LiveAudioEngine.h` | Owns one `AudioDeviceManager`, `beginManualAudioSetup()` and `finishManualAudioSetup()`, direct-route fence, accepted routing, independent clock and OUTPUT1 state | **Reuse these gates**, add a bounded device-configuration coordinator *around the existing manager*, never another manager/callback |
| `web/monitor.js` | Owns route draft, hardware epoch/revision, `routingDryApply`/native ACK | Keep as sole routing controller; on accepted device generation invalidate stale physical routing and request readback |
| Standalone HTML + tests | Inline mirror for `web/mixer-io.js` and browser native mocks | Keep byte-for-byte parity / rebuild embedded resource + add native device transaction fixtures |

## B. Physical topology and signal ownership

```text
Existing AUDIO I/O overlay (WebView2 — control/presentation only)
  DEVICE/DRIVER | INPUT ROUTING | OUTPUT ROUTING
          |
          | arMixAction typed requestId / epoch / inventoryGeneration
          v
Single native DeviceSetupCoordinator (control-only, serialized)
          |
          +-- existing engine_.devices() : JUCE AudioDeviceManager
          |      ASIO or WASAPI or legacy DirectSound + one direct callback
          |
          +-- existing engine safety: beginManualAudioSetup / direct fence
          |      secondary OUTPUT1 / independent-clock coordination
          |
          +-- post-open readback, actual AudioIODevice pins, configuration epoch
          |
          v
existing monitor.js route authority -> native routingDryApply/ACK -> ONE
prepared JUCE live mixer graph -> CH1–10, AUX1–4, MAIN/MONITOR, OUTPUT1
```

NO WebAudio DSP, second `AudioDeviceManager`, second plugin graph, unsolicited OS endpoint fallback, separate JS route controller, or hidden 64-channel mixer. OUTPUT1 is already a distinct approved WASAPI/ASRC output path; this milestone does not unify unrelated clocks or add a second capture graph.

## C. JUCE APIs: exact mapping and caveats

1. **Backend and device inventory:** existing `AudioDeviceManager::getAvailableDeviceTypes()`, `AudioIODeviceType::scanForDevices()`, `getDeviceNames(true/false)`, `hasSeparateInputsAndOutputs()`, `getTypeName()`. Names are the names JUCE currently accepts; never invent IDs. Driver names can duplicate; qualify by backend, direction and inventory generation. Scan can be slow and invoke vendor code.
2. **Active readback:** `AudioDeviceManager::getCurrentAudioDeviceType()`, `getAudioDeviceSetup()`, `getCurrentAudioDevice()`; only when a valid active `AudioIODevice` exists, `getInputChannelNames()`, `getOutputChannelNames()`, `getActiveInputChannels()`, `getActiveOutputChannels()`, `getCurrentSampleRate()`, `getCurrentBufferSizeSamples()`, `getInputLatencyInSamples()` and `getOutputLatencyInSamples()`. Native readback is source of truth, not requested dropdown values.
3. **Capabilities:** `AudioIODevice::getAvailableSampleRates()`, `getAvailableBufferSizes()`, `getDefaultBufferSize()`, `hasControlPanel()`. For *active* device, read with lifetime/thread ownership. For an *inactive* candidate, inspecting it may require constructing a driver-owned `AudioIODevice`; probe only with evaluated backend semantics and without opening a competing active device. Mark candidate capabilities `UNKNOWN_UNTIL_APPLY` if safe non-invasive inspection cannot be guaranteed; offer AUTO and verify after opening. Never merely hardcode a global list.
4. **Apply settings:** `AudioDeviceManager::AudioDeviceSetup` fields: `inputDeviceName`, `outputDeviceName`, `sampleRate`, `bufferSize`, `inputChannels`, `outputChannels`, `useDefaultInputChannels`, `useDefaultOutputChannels`. JUCE `setAudioDeviceSetup(candidate, true)` returns empty string only on success; `setCurrentAudioDeviceType(type, ...)` changes backend but is `void` and can implicitly open a default device. Therefore backend type changes and final selected setup must be ONE guarded control transaction with post-open readback and no temporary output-arm.
5. **ASIO pairing:** `!AudioIODeviceType::hasSeparateInputsAndOutputs()` -> bind capture/playback to the same compatible hardware interface and do not expose arbitrary split selection. For separable Windows Audio types, still require one supported synchronized direct graph. Existing independent-clock experimental path is separate user intent and must not be implicitly enabled.
6. **Physical pin bitmasks:** build `juce::BigInteger` masks exclusively from a currently verified device's reported channel names; avoid conflating selectable/available with **enabled**. Set `useDefaultInputChannels=false` / `useDefaultOutputChannels=false` when user specifies a mask. Re-read actual accepted masks after APPLY. Explicitly validate MAIN and MONITOR disjoint enabled physical output pins.
7. **Persistence:** save only after verified success (`createStateXml()` plus app's device-specific logical alias/show metadata). JUCE device name alone is not a guaranteed stable Windows endpoint ID; if a stable OS ID is not obtained through a separately validated mapping, flag ambiguity and ask for remap. Never silently substitute another microphone or re-arm output.
8. **Vendor control panel:** if `hasControlPanel()`, offer **DRIVER CONTROL PANEL** button. `showControlPanel()` may cause driver changes; on close, re-query actual setup and perform verified guarded reopen if necessary. It remains vendor-native GUI, not the normal JUCE selector.

Official API behavior:
- https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html
- https://docs.juce.com/master/structjuce_1_1AudioDeviceManager_1_1AudioDeviceSetup.html
- https://docs.juce.com/master/classjuce_1_1AudioIODeviceType.html
- https://docs.juce.com/master/classjuce_1_1AudioIODevice.html
- https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_utils/gui/juce_AudioDeviceSelectorComponent.cpp

**Important source finding:** JUCE's `setCurrentAudioDeviceType()` implementation may close an existing device and sleep ~1.5 s during backend transition (review the exact pinned JUCE version in this repository; the online mainline source is comparative). Never switch driver on every `onChange`; only submit APPLY after confirmation. Do not claim `MessageManager::callAsync` eliminates blocking if the callback invokes synchronous vendor code.

## D. Control/data contracts (proposed, not existing API)

All messages travel over existing `arMixAction` and `backend.addEventListener` bridge. Names may be refined, but one stable schema/versioned event family required.

```json
{"action":"audioDeviceInventoryV2","requestId":"uuid-or-monotonic-session-id"}
{"action":"audioDeviceCapabilities","requestId":"...", "inventoryGeneration":31,
 "backend":"ASIO","captureDevice":"reported input name",
 "playbackDevice":"reported output name"}
{"action":"audioDeviceApply","requestId":"...", "expectedDeviceEpoch":18,
 "inventoryGeneration":31,
 "config":{"backend":"ASIO","captureDevice":"reported name",
   "playbackDevice":"reported name","sampleRate":48000,"bufferSize":128,
   "enabledInputPins":[0,1,4,5],"enabledOutputPins":[0,1,2,3]}}
```

Responses (native -> WebView):
- `arMixAudioDeviceInventoryV2`: `{requestId, inventoryGeneration, backends:[{typeName,separateInputsOutputs,captureDevices[],playbackDevices[]}], active:{...}, scanState, errors[]}`.
- `arMixAudioDeviceCapabilities`: `{requestId, inventoryGeneration, backend, candidate, status:'VERIFIED'|'UNKNOWN_UNTIL_APPLY'|'ERROR', rates[], buffers[], pinLabelsIfVerified, hasControlPanelIfActive}`.
- `arMixAudioDeviceApplyAck`: `{requestId, expectedDeviceEpoch, deviceEpoch, ok, state:'ACCEPTED_MUTED'|'RESTORED_MUTED'|'RECOVERY_REQUIRED', errorCode, detail, accepted:{backend, captureDevice, playbackDevice, actualSampleRate, actualBufferSize, enabledInputPins[], enabledOutputPins[]}, changed:true|false}`.

Rules: native owns `requestId` idempotency and epoch revalidation; JS never forges ACK. Prefer bounded typed enumeration with stable string lengths, fixed maximum ports/entries and errors. `inventoryGeneration` is a snapshot generation, not an enduring device identity. No JS routing mutation on failed hardware apply. Reject invalid indices, duplicate/out-of-range pins, unreported device names, unsupported rate/buffer where verifiable, stale epochs, conflicting pending routing or secondary output, and unavailable callback ownership. Limit outstanding operations to ONE; read-only inventory refresh can coalesce.

## E. Hardware transaction — no partial-positive state

```text
IDLE -> ENUMERATING -> DRAFT_READY -> REVIEW
                                    |
                              user APPLY DEVICE
                                    v
      VALIDATE -> DISARM_ALL_OUTPUTS -> QUIESCE_ROUTE/SECONDARY
            -> SUSPEND_DIRECT_FENCE -> CHANGE_BACKEND_IF_NEEDED
            -> SET_AUDIO_DEVICE_SETUP -> READBACK / VERIFY
                  |                         |
                error                     success
                  v                         v
       RESTORE_PREVIOUS_SETUP       NEW_DEVICE_EPOCH
       + VERIFY READBACK           REBUILD VERIFIED PIN INVENTORY
                  |                 INVALIDATE PHYSICAL ROUTE DRAFT
           verified | failed       RE-ESTABLISH SAFE UNPATCHED SESSION
                  v       v                 |
       RESTORED_MUTED  RECOVERY_REQUIRED    v
                              ACCEPTED_MUTED (user manually arms later)
```

Detailed ordering:
1. Capture a **complete accepted snapshot**: native type, JUCE `AudioDeviceSetup`, verified pin masks, accepted epoch/revision and dependent MAIN/MONITOR/OUTPUT1 obligations. Staged UI change has zero audio side-effects.
2. Preflight on control owner: validate session, request generation, no pending graph apply/Show/VST structural handoff, no incompatible independent-clock transition, route/output restrictions. If armed, show explicit warning and require operator-confirmed disarm before issuing the transaction. Never internally turn an armed sound system back on.
3. Call existing `engine_.beginManualAudioSetup()` only after safe disarm/secondary handling; this currently mutes and suspends the direct fence and disables dry patch. Ensure every relevant secondary output is truly silent; `setOutputMuted(true)` alone must not be assumed sufficient for independent secondary sink. Avoid destroying VST identities when hardware changes.
4. Serialize **one** backend/device setup operation. If type changed, call `setCurrentAudioDeviceType` under the device owner and then `setAudioDeviceSetup` with explicit chosen options; no unverified intermediate events ever become LIVE. No processing in RT callback beyond existing stop/start/atomic guards.
5. Verify `getCurrentAudioDevice` exists/isOpen, backend and device names match a legal selected choice, actual sample rate/buffer, required enabled pin masks (not silently truncated), and engine's `finishManualAudioSetup()` has restored an authorized safe session; rebuild actual input/output pin inventory and bump epoch. Handle default/auto negotiation by reporting actual values. Do not demand equality for AUTO rate/buffer; do demand driver-reported effective values and explicit sufficient ports.
6. Success: persist accepted snapshot atomically only *after* readback; clear stale patch drafts by epoch, notify one ACK with active state **MUTED**. User revalidates MAIN/MONITOR and explicitly arms.
7. Failure: attempt rollback to prior known-good backend/setup once under same serialized executor, verify it is genuinely open and masked, **still MUTED**. If rollback fails, enter **RECOVERY_REQUIRED** with outputs silent, no fake restored device; offer retry/reselect. Do not loop retries or automatically choose arbitrary defaults.
8. Device removal while idle or applying increments inventory/device generation and revokes stale routes. Defer hardware mutations until current transaction ends. Drop stale JS deliveries by requestId/generation. No listener uses destructed component pointer.

## F. Threading and freeze-proofing decision

**Engineering truth:** driver scanning, ASIO COM/driver calls and reconfiguration are not guaranteed interruptible. A queued `callAsync` on the JUCE message thread is still a synchronous block *while executing*. A timer/watchdog cannot forcibly cancel a hung vendor driver inside that thread; pretending otherwise would be a design defect.

**Implement in this order:**
- First, read the pinned JUCE 8 implementation and audit the thread affinity of ASIO, WASAPI, `AudioIODeviceType` and `AudioDeviceManager` plus existing `LiveAudioEngine` callbacks. Choose **one** serialized lifecycle owner; all manager operations must be on its supported thread, with RAII request cancellation and a `SafePointer`/weak-token on UI delivery. No detached per-click threads.
- For read-only discovery, prefer a **separate bounded scanner** if its backend objects can safely be created outside the live manager and thread affinity supports it; post immutable results back. If that is unsafe for ASIO, use an isolated *helper process* for potentially hanging enumeration; the helper must never secretly open a live audio graph or own the main engine.
- For actual APPLY, keep **one** JUCE manager on its correct supported thread. If a specific driver requires blocking message-thread operations, show a guarded, non-interactive apply state and measure the delay; do not promise absolute GUI responsiveness. Long-term hard isolation of a wedged vendor device requires a separate audio-host process and IPC (out of scope; would be an explicit architecture milestone, not a naive worker).
- GUI paints instantly from cached accepted state; receives queued immutable results; avoid full overlay rerender for status ticks. Throttle human refresh, coalesce identical scans, cancel stale responses, never UI wait/spin on native.
- Separate output silence gate must be RT-safe and independent of whether configuration ACK reaches JS. Native safety is never delegated to UI state.

## G. Failure and lifecycle matrix

| Failure | Native response | UI response |
| --- | --- | --- |
| Backend not present | reject before mutation | unavailable / explanation |
| Candidate disappears | stale inventory or open error -> rollback | REFRESH / REMAP REQUIRED |
| Rate/buffer rejected | rollback once; no fallback substitution | rejected with actual supported choices |
| ASIO busy / driver error | rollback or RECOVERY_REQUIRED | outputs muted; retry, no LIVE |
| Invalid active pin mask | reject or re-open rollback; never claim accepted truncated mask | mark unavailable pins |
| User closes setup during APPLY | operation retains native owner; only UI listener released | on reopening, fetch current transaction/readback |
| Duplicate/late ACK | reject by `requestId` + generation | no stale UI overwrite |
| Device reappears with same name but different pins | new epoch and resolution check | REMAP REQUIRED, no silent route |
| Driver control panel changes rate | requery capabilities, invalidate stale config | change notice + explicit review |
| Startup saved device missing | safe silent device state; no automatic substitute/output-arm | choose device |

## H. Verification contracts and explicit limitations

Contract tests: native device fake with `AudioIODeviceType` behavior or test coordinator adapter; race tests for missing device, invalid channel mask, busy, partial open, wrong readback, rollback failure, duplicate request, stale generation, close-during-apply and restart. GUI Playwright fake-driver tests for same. Windows MSVC/CTest and packaged EXE with actual ASIO and WASAPI test loops. Instrument `scan_ms`, `apply_ms`, `rollback_ms`, `deviceEpoch`, `callbackStop/StartCount`, UI heartbeat and XRUN evidence. Watch for accidental callback multiplicity, leaked device references/COM objects and L2 fader regressions.

**Not delivered by this ARD:** implementation itself, live-armed source hot-switch, arbitrary simultaneous multi-interface capture, actual full 64-channel mix, GROUP/DCA/MATRIX/CUE, ASIO vendor GUI styling, guaranteed zero driver hang, measured zero latency or #15 physical crackle clearance.
