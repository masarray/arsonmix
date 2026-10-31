# HANDOFF — Implement Custom Device Setup in the existing ArSonMiX Audio I/O Matrix

**Revision:** 2026-10-31 · **Execution handoff: DEVICE1 core implemented in #228; complete remaining hardware FAT, regression and follow-up hardening**  
**Start from current merged main, not an old PR branch.** Read in order: [product truth](PRODUCT_TRUTH_CROSS_THREAD_20261031.md), [PRD](PRD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md), [ARD](ARD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md), [PATCH2 ARD](ARD_HARDWARE_AUDIO_IO_MATRIX_20261030.md), [SOURCE1/#224](SOURCE1_NATIVE_MUTED_FAST_COMMIT_20261031.md), `AGENTS.md`.

## 0. Product and source lock

**Deliverable:** AUDIO DEVICE button -> existing `web/mixer-io.js` overlay -> choose actual ASIO/WASAPI/device/rate/buffer/enable pins -> **APPLY DEVICE -> real JUCE native readback -> OUTPUTS MUTED**. Same overlay's existing INPUT/OUTPUT routing matrix then operates against verified active hardware pins. Remove routine reliance on `AudioSetupWindow`/native JUCE selector, not JUCE engine itself.

**Do not touch** RAIL3 fader design, pinned MASTER, FX host/plugin identity, Studio Show, actual 10CH+4AUX graph, AUX live gain, DYN offline experiments, full web layout, or OUTPUT1 architecture just to deliver device setup. #224 already merged 64-pin selector and muted SOURCE quick commit; do not redo its route controller. No separate mixer graph, no new Browser AudioContext, no second AudioDeviceManager.

## 1. Pin down source and reconciliation before edits

Inspect the exact main HEAD and these functions/sections:
- `src/Main.cpp`: `AudioSetupWindow`, `showDeviceSetup()`, `audioDeviceInventory`, `audioSetup` command dispatch, `changeListenerCallback`, device-settings save and `audioDevice...Ack` emission convention.
- `src/LiveAudioEngine.h`: `devices()`, `beginManualAudioSetup()`, `finishManualAudioSetup()`, `approveDirectRoute()`, `setIndependentClockEnabled()`, `secondaryOutput_` arm/disarm, direct fence, device start/stop listener, routing epoch and currently accepted physical masks.
- `web/mixer-io.js`: `openIO('device')`, DEVICE tab in `refreshIO()`, `data-io-advanced`, `arMixAudioDeviceInventory` event, input/output matrices, expensive `lastSignature` diffing, `quickSourcePending`.
- `web/monitor.js`: existing source/route draft epoch, `__arMixRouteView`, `__arMixApplyMixerDraft`, routing native ACK; maintain one owner and coordinate device epoch change.
- `web/bridge.js`, `CMakeLists.txt`, standalone `AR-MIX-Live-FX-Host-Standalone-v0.3.html`, `tests/mixer_io_smoke.py`, `tests/ui_smoke.py`, native CTest targets.
- Pinned JUCE 8 revision, not online `master`, because JUCE backend behavior/thread affinity can differ; inspect `setCurrentAudioDeviceType`, `setAudioDeviceSetup` and selector's `updateConfig` in that exact checkout.

**Root cause already verified:** existing `audioDeviceInventory` scans types and emits *names only*. The custom DEVICE view has no native APPLY; `data-io-advanced` still invokes the ugly native selector. A successful click on that button is not acceptance of a custom setup feature.

## 2. Delivery work packages — one coherent release milestone

### WP1 — Native model and safe inventory

Create a small strongly typed `DeviceSetupSnapshot` and `DeviceSetupRequest` / `DeviceSetupAck` adjacent to existing native device code (prefer `engine/device/`; avoid giant duplication inside Main.cpp). Expose one coordinator through the existing `ConsoleWindow` owner; it references `engine_.devices()`, not constructing a new manager.

- On user open/refresh: return backend `typeName`, `hasSeparateInputsAndOutputs`, capture/playback device names, active configuration, and inventory generation. Preserve actual case-sensitive JUCE names for the driver.
- Support capabilities of the **active device** first: real input/output channel names, active pin masks, supported rates, supported buffer sizes and control-panel capability.
- For **inactive candidate** capability discovery: investigate the exact JUCE type's `createDevice()` safety, lifetime and side effects. If safe, probe with a bounded independent object under tested ownership; otherwise return `UNKNOWN_UNTIL_APPLY` and let AUTO select, verified after APPLY. Do not open multiple real audio callbacks to populate a dropdown.
- Keep the accepted snapshot immutable for UI projection. Do not map multiple independent WASAPI endpoints into one audio stream by merely listing them. Cap reported entries/pin arrays; normalize duplicate device display names using backend+direction, without fabricating stable IDs.
- Add unit tests for phantom/duplicate device names, mask bounds, nonseparable ASIO and empty/no-device data.

### WP2 — Transactional native APPLY DEVICE

New bridge action `audioDeviceApply` and native event `arMixAudioDeviceApplyAck`, typed/versioned per ARD, with requestId, inventoryGeneration, expectedDeviceEpoch, requested backend/capture/output/rate/buffer/pin mask.

1. **Preflight:** reject any stale generation, pending native routing apply, Show structural commit, incompatible independent clock, invalid pin pair, impossible rate/buffer (if certified), or secondary output not safely disarmed. User must consciously confirm the mute. Block all duplicate APPLY until definitive ACK.
2. **Snapshot** last actually working setup/driver, including output state and route epoch. Never assume the currently staged JS text is a working old device.
3. **Silence** MAIN/MONITOR and independently clocked OUTPUT1; ensure secondary device and route owned transition safely. Reuse `beginManualAudioSetup` and one direct fence; audit whether it covers secondary output and repair gap before attempting device restart.
4. **Reconfigure** one serialized native owner: backend change via `setCurrentAudioDeviceType` if necessary, then exact `AudioDeviceSetup` via `setAudioDeviceSetup(config,true)`. Explicit requested masks set `useDefault*Channels=false`; use default flags when AUTO selected.
5. **Readback & authorize** actual `AudioIODevice` and `AudioDeviceSetup`, active masks, rate and buffer, then `finishManualAudioSetup`. The accepted hardware epoch must be newly published to existing route controller; all physical pin references from previous epoch are considered stale until revalidated.
6. **Persist** only when final readback is valid; update user/device profile + JUCE state as an atomic/last-good workflow. Deliver ACK `ACCEPTED_MUTED`. Never call auto unmute.
7. **Rollback** exactly one best-effort previous backend/setup restore if any step fails, verify readback and emit `RESTORED_MUTED`; if restore fails emit `RECOVERY_REQUIRED`, keep outputs silent, show recovery controls. Never commit a half-valid device or blindly select Windows default.
8. **Reconcile UI** by requestId/generation and accepted snapshot. Closing overlay while APPLY is pending must not delete or race the native operation.

Important: `setCurrentAudioDeviceType` can **implicitly open and block**; `setAudioDeviceSetup` is synchronous. Do not fire on select-change. An async JS bridge does not eliminate main-thread blocking. Implement supported-thread ownership only after JUCE backend/thread-safety study; avoid casual worker wrapping or `std::async` around `AudioDeviceManager`. Record native call durations and test real ASIO. Add process-isolated discovery if field measurements establish vendor scan hangs. True hard recovery from an in-process wedged device requires a larger separate-host architecture, not a fake timer.

### WP3 — Replace DEVICE view inside existing matrix (no new dialog)

In `web/mixer-io.js`, keep single `arIOOverlay`, 3 tabs and the existing input/output rows. Replace informational badge/card list with compact, real form:
- **Driver Type:** detected ASIO/WASAPI/DirectSound selector (disabled if absent).
- **Audio Interface / Capture:** exact candidate names. **Playback:** exact names only when `hasSeparateInputsAndOutputs` true; otherwise tied duplex interface, clearly displayed.
- **Sample Rate + Buffer:** real available options only, or `AUTO / confirmed after APPLY`. Do not assume sample rate 48 kHz or buffer 128 just because typical.
- **Enabled Hardware Pins:** compact bounded checklist or scrollable two-column list, shows driver-reported labels and indices; distinguish available, enabled draft and active after readback. Preserve nonadjacent stereo matrix L/R options.
- **Driver Control Panel:** available only if active device returns `hasControlPanel()`. Route to native owned driver panel with safe readback, not the JUCE selector.
- **Footer:** `REFRESH`, `REVERT`, `APPLY DEVICE`, concise status. Preserve existing **APPLY PATCH** only for input/output routing tabs; prevent accidentally submitting a route APPLY from DEVICE view.
- **States:** loading inventory, no device, draft, applying, accepted-muted, restored-muted, recovery-required, stale/disconnected, remap required, web preview only.

Actions are event-driven with native ACK. Coalesce repeated inventory requests, generation-fence stale responses. Render the device form from cached snapshot (no re-render at cursor movement); no blur/filter-heavy overlay, no mutation of the 11-strip layout. Treat UI inventory as *read-only* until APPLY. Render only real hardware; no fake MAONO channels or pretend hardware test in browser preview.

### WP4 — Bridge, tests and operator FAT

Bridge schema validation in native command dispatch: reject oversized/missing/negative fields, invalid arrays and arbitrary "device" names. Make ack delivery bounded; never deliver a pointer to ephemeral JUCE owner. Unit tests with fake device service adapter and browser fake native event; no unsupported OS hardware needed to exercise failures.

**Chrome/Playwright acceptance** `tests/mixer_io_smoke.py` (and standalone parity):
- Open AUDIO DEVICE -> DEVICE tab and actual named dropdown, not "SELECT DRIVER / DEVICE" native popup.
- Inventory ASIO 8 inputs/4 outputs, WASAPI 2in/2out; verify proper device pairing and enable masks.
- Simulated apply PENDING, exactly ONE device request on APPLY, no request during dropdown changes; inject matching positive ACK -> ACTIVE/MUTED state and only then update matrix pins.
- Inject rejected/stale/out-of-order ACK; ensure old accepted device still displayed, controls recover, no native route publication on failure.
- Device removal while dialog open, close/reopen during pending APPLY, no device/unsupported rate and web preview.
- Verify INPUT Input1->CH3 and virtual driver-reported L/R->CH6 still issue existing ONE `routingDryApply` when safely muted. 11 mixer strip positions unchanged; no WebAudio audio output.

**Windows CTest**: fake backend/manager adapter with ready, failure, rollback, invalid pin, rate mismatch, reopen and epoch fences; JUCE ownership test and single-callback invariant. On actual MSVC executable confirm APIs compile with pinned JUCE version and selected flags. Regression tests for OUTPUT1 secondary disarm, MAIN/MONITOR independent routing, no accidental FX state deletion, offline DYN1B unchanged. Use bounded CI runs: cheap static/JS tests first, then ONE Windows native CI.

**Physical hardware FAT** (mandatory for declaring this milestone field-ready):
1. MAONO PS22 driver: ASIO selectable, real channel labels; 44.1/48 kHz and 64/128/256 only where driver reports support; observe actual enabled masks, buffer and readback.
2. Input 1 -> CH3, driver-exposed virtual L/R -> CH6; test correct L/R/nonadjacent mappings by tone injection/recording, no channel swap.
3. MAIN Out1/2 and optional physical MONITOR Out3/4 (only if reported); verify disjoint pins. OUTPUT1 remains optional independently-clocked destination, not auto-enabled.
4. WASAPI real endpoint, driver busy, device absent, replug, sample-rate reject, app restart, saved profile, missing pin => REMAP REQUIRED.
5. 50 controlled APPLY/open-close iterations (include 10 driver switch attempts if supported), instrument GUI latency, start/stop count, memory growth, native duration; active playback must stay muted after every reconfiguration. Any vendor hang is recorded, not falsely labeled PASS.
6. **Issue #15 0FX crackle** is a separate physical release gate; do not bury it under a green Windows build. Preserve logs with device type/rate/buffer/driver callback timing and actual audible outcome.

## 3. Engineering acceptance checklist (PR reviewer)

- [ ] New DEVICE controls actually drive `AudioDeviceManager` and reflect **native readback**.
- [ ] No ordinary path opens `AudioDeviceSelectorComponent`; vendor-native panel permitted as distinct action.
- [ ] No second manager / callback / route state owner; no filesystem, allocating or waiting in RT callback.
- [ ] UI-only changes do not reopen hardware. APPLY only, one serialized request; `requestId` and epoch protect against stale ACK.
- [ ] Rollback success and rollback failure have separate verifiable outcomes; all physical/secondary outputs remain DISARMED.
- [ ] Honest unsupported caps, ASIO duplex rules, active/enabled pin distinction, no invented virtual channel names.
- [ ] Session persistence and Show remap never silently bind new microphone or output; no unsolicited default fallback.
- [ ] FX leases, SENDS, AUX1–4, MAIN, MONITOR, OUTPUT1, L2/RAIL3 preserved.
- [ ] No pointer capture/use-after-free on close; no duplicate listeners, unmanaged worker threads or unbounded scan.
- [ ] Playwright + native Windows CTest pass; physical FAT results attached separately; #15 explicitly open until proven fixed.
- [ ] `web/mixer-io.js` and standalone inline `arsonmix-mixer-io-js` are identical as required by existing smoke tests; generated packaged WebView assets updated.

## 4. Merge and deployment discipline

One coherent implementation PR, not sequential scattered cosmetic PRs. Rebase on current `main` before code. Start with the native typed contract + unit tests, then custom DEVICE form and browser tests, finish with Windows compile/runtime tests. Do not launch heavy CI for documentation-only PR. Do not merge driver logic that compiles but has no actual working selection path; do not call a mock-browser screenshot physical hardware proof. Code acceptance is **custom selectable audio device that really opens in the shipped EXE**, not merely a nicer selector UI.

**Safety fallback during migration:** keep legacy `audioSetup` dispatch behind hidden engineering recovery flag only until custom native APPLY passes Windows+physical FAT, never as the primary visible button. If a driver requires a proprietary vendor panel, expose only its actual `AudioIODevice::showControlPanel()` capability.

## 5. External evidence and technical sources

- JUCE manager and saving accepted user chosen setup: https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html
- `AudioDeviceSetup` sampleRate/buffer/masks and default flags: https://docs.juce.com/master/structjuce_1_1AudioDeviceManager_1_1AudioDeviceSetup.html
- JUCE backend enumeration and separate input/output: https://docs.juce.com/master/classjuce_1_1AudioIODeviceType.html
- JUCE runtime capabilities/readback: https://docs.juce.com/master/classjuce_1_1AudioIODevice.html
- JUCE selector source for behavior study: https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_utils/gui/juce_AudioDeviceSelectorComponent.cpp
- Fender Studio Pro setup and software I/O: https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm
- Waves LV1 patch operator workflow: https://www.waves.com/emotion-lv1-tutorial-2-2-using-the-patch-window
