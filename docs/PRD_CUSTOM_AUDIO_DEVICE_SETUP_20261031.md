# PRD — Custom Audio Device Setup inside the existing ArSonMiX I/O Matrix

**Revision:** 2026-10-31 · **Status:** PROPOSED IMPLEMENTATION CONTRACT / NOT YET CODED · **Scope:** Windows realtime mixer, no DAW/editor.  
**Read with:** [ARD](ARD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md) and [handoff](HANDOFF_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md). Existing [PATCH2 PRD](PRD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), [PATCH2 ARD](ARD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), [product truth](PRODUCT_TRUTH_CROSS_THREAD_20261031.md), and #224 are authoritative for routing/engine constraints.

## 1. Problem proven from the repository

- `web/mixer-io.js` has a custom **DEVICE / DRIVER, INPUT ROUTING, OUTPUT ROUTING** window and a native device-name inventory, but selecting the driver/device/rate/buffer still calls `audioSetup`.
- `src/Main.cpp` handles `audioSetup` by opening the native `AudioSetupWindow` / `juce::AudioDeviceSelectorComponent`. Current `audioDeviceInventory` contains backend names plus input/output **device names**, not per-device capabilities or an APPLY command.
- Existing `web/monitor.js` is the one trusted source/patch controller; native `routingDryApply` + `arMixRoutingLabAck` are the established route acceptance path.
- #224 merged 64-*selectable device pins* and muted source auto-submit; it did **not** replace the native picker. Neither source hot-switch while output ARMED nor physical MAONO/zero-FX issue #15 is certified.
- Result: a promising matrix that cannot function as complete custom device configuration without opening a visually unrelated JUCE window. **No second I/O setup, second audio engine or fake web-audio bridge is acceptable.**

## 2. Product outcome and hierarchy

Operator opens **AUDIO DEVICE** and lands on the existing **AUDIO I/O > DEVICE / DRIVER** tab. From one compact, premium WebView2 window they can:
1. Choose a *real reported* backend: **ASIO** (preferred for supported pro hardware), **Windows Audio / WASAPI** (supported), **DirectSound** (legacy, explicitly labeled higher-latency).
2. Choose *real capture and playback device names* compatible with that backend. ASIO generally uses one duplex interface; only show independent capture/playback selection if the backend's native type reports support. Never merge separate clocks/devices into a direct JUCE graph.
3. Choose sample rate and block/buffer size from **verified candidate capabilities**; if unavailable until open, show “AUTO / verify on APPLY”, not fabricated 44.1/48/96 kHz or 64/128/256 options.
4. Enable selected driver-reported input/output physical pins and see their authentic labels. UI limit 64 is a selection bound, **not** 64 mixer channels. Default enabled pins are read from native state; no phantom “virtual” ports.
5. Review requested changes and press **APPLY DEVICE** once. Explicit MUTE/disarm, native transaction status, validation and positive ACK follow. Outputs **remain muted**; user separately arms them in existing Signal Check.
6. Immediately use the **existing** INPUT ROUTING matrix for physical input -> CH1–10, OUTPUT ROUTING for existing independent MAIN and MONITOR pairs; optional OUTPUT1 separately retains its existing secondary WASAPI ASRC policy. The mixer SOURCE field remains the routine path once the device is configured.

Navigation: single overlay, compact 3 tabs; DEVICE/DRIVER primarily a device selector plus rate/buffer and pin masks. INPUT/OUTPUT matrices are not removed or rebuilt. Preserve the mixer's L2 11-strip geometry, RAIL3 fader, FX, AUX1–4, Show, independent MONITOR and OUTPUT1. No heavy blur, animated gradients or full DOM redraw on every meter frame.

## 3. Exact interaction / state copy

**DEVICE**: native-reporting backend list; CAPTURE DEVICE; PLAYBACK DEVICE (locked/same for nonseparable backend); SAMPLE RATE; BUFFER; INPUT PORTS; OUTPUT PORTS; native capability provenance/status; optional **DRIVER CONTROL PANEL** (only if device reports it); **REFRESH**; **REVERT**; **APPLY DEVICE**.

Status language:
- `NO DEVICE`: nothing active or driver unsupported.
- `ACTIVE / VERIFIED`: readback from JUCE active setup and open/playing device, not only JS selection.
- `DRAFT`: controls differ from the accepted native snapshot.
- `APPLYING — AUDIO MUTED`: device reconfiguration in progress; duplicate APPLY disabled.
- `ACCEPTED — OUTPUT MUTED`: native readback matches requested essentials; route pins revalidated.
- `REJECTED — OLD DEVICE RESTORED, OUTPUT MUTED`, `RECOVERY REQUIRED — OUTPUT MUTED`, `DISCONNECTED — OUTPUT MUTED`, `REMAP REQUIRED`: actionable, never claim LIVE.

**Important**: device setup APPLY is distinct from patch APPLY. A backend/sample/buffer/physical-pin change is structural, not an ordinary live fader/send parameter. Never silently auto-rearm MAIN, MONITOR or OUTPUT1.

## 4. Essential operator scenarios

| ID | User journey | Required result |
| --- | --- | --- |
| D01 | First install with no device | Empty-state explains Windows/ASIO driver absent; REFRESH works; never fabricate devices or crash |
| D02 | MAONO PS22 present with ASIO | ASIO -> actual interface name -> native-supported sample/buffer -> actual enabled pin labels -> APPLY -> native readback |
| D03 | Windows Audio / WASAPI | Lists actual capture/playback endpoints; only legal independent pairing; mode is reported honestly; respects host device exclusivity/OS conflicts |
| D04 | From real capture Input 1 to CH3 | Once DEVICE is accepted, existing SOURCE / INPUT ROUTING binds Input 1 -> CH3 using native route ACK |
| D05 | Virtual stereo -> CH6 | Available only if chosen driver reports both input pins; preserve L/R identity, including nonadjacent pair by existing advanced matrix |
| D06 | MAIN 1/2 + MONITOR 3/4 | Allowed only when all four distinct **enabled** playback pins actually exist; no routing to imaginary AUX hardware pins |
| D07 | Change sample rate/buffer/driver | One explicit transaction, outputs disarmed, current patch invalidated/rechecked and audible re-arming always manual |
| D08 | Unsupported rate or missing input | No fabricated fallback, actionable negative ACK and rollback/recovery status; last accepted profile preserved |
| D09 | Unplug/replug during dialog or APPLY | Stale generation denied, no stale accepted route, no automatic new endpoint or output-arm |
| D10 | Device used by other application / corrupt ASIO driver | UI returns concrete error/recovery state where possible; native GUI never intentionally blocks audio thread |
| D11 | Rapid repeated changes / close overlay mid APPLY | Last pending request cannot mutate UI from stale response; no concurrent device changes or use-after-free |
| D12 | Relaunch / import Show / change PC | Load accepted per-device configuration only if resolved unambiguously; missing inputs show REMAP REQUIRED; never silently substitute another microphone |

## 5. Nonnegotiable quality, reliability and performance

- **One authoritative device owner**: existing JUCE manager; **one authoritative route owner**: `monitor.js` + native routing engine. UI is a projection, not a DSP process.
- No re-enumeration on every paint, input mousemove or frequent status telemetry. Refresh only on open, manual refresh, device-change notifications, or controlled debounce; coalesce duplicate requests.
- Never call device scan/open/close, JSON decoding, VST3 loading, file I/O or synchronous WebView bridge from the audio callback. No blocking waiting on JS ACK.
- The client must remain keyboard and mouse operable during *ordinary* inventory/capability work. Vendor ASIO scan/open calls may block inside a driver: document the limitation and adopt worker/process isolation only after JUCE thread-affinity and lifecycle evaluation. An async-looking WebView call is **not** proof a blocking native operation is harmless.
- Hard upper bounds: one device APPLY at a time; one latest-generation inventory snapshot; bounded pin array (64/side for current supported UX), rate/buffer options and event payload sizes; no unbounded tasks/queues.
- Device switching does not promise glitchless continuous audio: deliberate **OUTPUT MUTED** is safer for live production. Only true source-only hot patch can later bypass global mute after its independent native de-click FAT.
- Performance objective: initial overlay should paint from last accepted snapshot without waiting for driver; native discovery updates asynchronously where safe; prove responsiveness on Intel iGPU/server PC and ordinary Windows desktop. Measure UI input delay, driver-call duration and recoverability instead of inventing a zero-freeze guarantee.

## 6. Definition of Done / release gates

**Functional G1:** Windows executable selects real ASIO and WASAPI device *inside* the existing custom matrix, without JUCE selector as normal path; actual rate/buffer/input/output device and enabled pins are changeable and confirmed from native readback.

**Safety G2:** Typed transaction with `requestId`, expected device generation/epoch and guarded config; no auto-unmute; stale/duplicate ACK rejected; an error restores previous valid configuration if possible, otherwise explicit safe silent state.

**Integration G3:** Existing INPUT/OUTPUT PATCH, CH 1–10, AUX 1–4, MAIN/MONITOR, OUTPUT1, FX lease, Show, L2/RAIL3, native route epoch/revision ACK are unbroken. Changed hardware invalidates unsound physical pin mappings, not VST state unnecessarily.

**Performance G4:** Repeated open/close and 50 controlled device selection attempts without memory/resource growth, duplicate callback, stale preview, crash or uncontrolled UI freeze. Driver-specific hangs cannot be falsely certified away.

**Evidence G5:** JS/Chromium fake driver, Windows CTest/ASIO-capable real device test and physical PS22/alternate WASAPI test. Physically measure input audibility, L/R routing, output MAIN/MONITOR, 44.1/48k and 64/128/256 where actually supported, disconnection and 0FX crackle #15. CI alone does not qualify hardware.

## 7. Official research used (design intent vs existing implementation)

- JUCE `AudioDeviceManager`: https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html
- JUCE `AudioDeviceSetup` input/output names, channels, rates, buffer/default semantics: https://docs.juce.com/master/structjuce_1_1AudioDeviceManager_1_1AudioDeviceSetup.html
- JUCE `AudioIODeviceType` scan and separable input/output: https://docs.juce.com/master/classjuce_1_1AudioIODeviceType.html
- JUCE `AudioIODevice` reported channels/rates/buffers/control panel: https://docs.juce.com/master/classjuce_1_1AudioIODevice.html
- JUCE selector implementation, used only to crosscheck behavior: https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_utils/gui/juce_AudioDeviceSelectorComponent.cpp
- Fender Studio Pro audio device: https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Set_Up_Your_Audio_Device.htm
- Fender Studio Pro reusable software I/O matrix: https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm
- Waves LV1 patch semantics: https://www.waves.com/emotion-lv1-tutorial-2-2-using-the-patch-window
