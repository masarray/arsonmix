# ArSonMiX — Hardware Audio I/O Matrix Architecture
**Baseline:** `main@0801cc8f661001e0e97e0783f9dbaec7522613e7`, 2026-10-30. **Implementation:** branch `feat/studio-hardware-io-matrix-20261030`.

## Source audit (pre-change)
- `src/LiveAudioEngine.h` owns JUCE AudioDeviceManager/ASIO priority, validated direct device and optional independent WASAPI clock path. `src/Main.cpp` already offers a JUCE AudioDeviceSelectorComponent and `audioDeviceInventory` action.
- `web/monitor.js` is the **only** control-plane owner of `draftRoutes`, `draftMainOutputs`, accepted device epoch / native revision, `__arMixStageChannelInput`, `__arMixStageMainOutputs`, and `__arMixApplyMixerDraft`. `routingDryApply` is the sole native commit path. `arMixRoutingLabAck` and status refresh prove which revision became live.
- `web/mixer-io.js` was a 10-row INPUT dropdown and two MAIN OUTPUT selectors, with pre-existing native routing and AUX-send controls; I/O already existed, but **did not resemble the supplied Studio One logical/hardware crosspoint**.
- Native MS3A output patch has a physical MAIN pair. Concurrent MS3B PR #203 targets independent MONITOR physical outputs; do NOT fake MONITOR into this PR or overwrite its graph.
- `sc220-live/assets/ui/sc220_mkii_device_alpha.png` is a SC220-specific product render (302 kB), **not guaranteed to be the attached sound card**. Instead, a tiny original `web/assets/audio-interface.svg` generic illustration is in the native JUCE resource provider and browser static directory, with an honest descriptive alt text.

## Ownership and data flow
`JUCE AudioIODevice` → native route status/port snapshot → `web/monitor.js` shared route view → `web/mixer-io.js` matrix → **existing monitor.js staging function** → MUTE safety preflight → `routingDryApply` → native graph preparation and revision/epoch fence → ACK → matrix updated from authoritative native status. No new audio callback, audio processor, JS DSP, device enumerator or worker.

## UI state machine
- Logical route values: NONE, MONO(left), STEREO(left,right), BUS (full ROUTING workspace). Separate `channelFormat` stays untouched except native default on newly bound source; changing processing width remains a different control.
- UI-only `matrixDraft` stores incomplete selection (epoch, source mode, L/R pins) and `matrixSide` stores which side is being edited. Incomplete stereo never enters `monitor.js` and cannot be submitted from the open modal. When two distinct enabled pins are chosen, call existing `__arMixStageChannelInput` once and clear the UI-only half-pair. Clicking NONE stages a valid unpatch.
- INPUT pin list is `routeView.ports[]` integer `port` values; OUTPUT pin list is `routeView.outputPorts[]`. De-duplicate for columns; DO NOT treat array index as hardware pin. Display friendly labels and physical pin number. Page a maximum of 8 verified pins without creating empty phantom columns. There is no fixed maximum physical input port count in the UI.
- MAIN physical output stage copies current draft/accepted pair, replaces requested side, and calls `__arMixStageMainOutputs`. Reject duplicated/disabled pin. Physical output is still muted on APPLY, and old ports remain silent after ACK.
- A device `epoch` change clears UI pending pin selections and page offsets. APPLY/PENDING and armed output disable controls as appropriate; no implicit device re-open and no auto-unmute. REVERT uses `__arMixRevertMixerDraft`.
- Distinguish green verified accepted crosspoints vs blue user draft; a blue crosspoint is not audible. UI-only incomplete selections are pending, not a graph revision.

## Performance, packaging, multi-thread integration
- Modal DOM constructed only on open, native topology/status identity, route draft mutation or explicit page/tab interaction; no animation/pointer loops or blur.
- Retain MIX rack, 11 fader geometry, existing toolbar and native VST3 editor ownership; model and matrix should not duplicate AUDIO SETUP state.
- `CMakeLists.txt` bundles one original SVG; `src/Main.cpp assetFor` serves `image/svg+xml` via JUCE WebView2 resource provider in the packaged EXE. Do NOT use a remote CDN or desktop-only file path.
- Source conflict watch: MS3B #203 may edit `src/Main.cpp` and CMake. Rebase/reconcile actual changed paths before merge. No opening broad stale #115/#117/#128.
- Run existing `tests/mixer_io_operator_contract_test.mjs`, `tests/mixer_io_smoke.py`, Windows native tests/packager, and user low-end PC pointer/zero-FX hardware FAT. GitHub Actions passing certifies source/build, not sound quality.

## Follow-on
DYN1B/DYN2 replaces fixed 10 live rows with common ChannelId-based runtime model and virtualized matrix row list when the native DSP is ready. MS3B adds only authentic additional output buses. Port labels are display information; stable device+pin identity must be persisted/rebound by native session handling, never mere GUI label comparisons.
