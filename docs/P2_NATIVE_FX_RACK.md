# AR MIX Studio P2 — Native VST3 Catalog and FX Rack (Experimental)

## Try on Windows
1. Open the GitHub Actions build artifact `AR-MIX-Studio-P2-Windows-x64` from the successful workflow.
2. Unzip, open `AR-MIX-Studio-P2.exe`. Audio monitoring is OFF at startup.
3. In **SETUP**, choose **OPEN ASIO / WASAPI DEVICE SETTINGS**, select an input+output interface, 48 kHz, and 128/256 samples to start.
4. In **FX RACK**, click **RESCAN VST3** to search system VST3 locations, **ADD PLUGIN FOLDER** for an additional installation directory, or **IMPORT VST3 FILE** for an individual `.vst3` file/bundle.
5. Results appear in the persistent native catalog. Search by plugin name or vendor. Click **+ ADD** to insert the plugin into the actual audio chain. Repeat up to 8 plugins.
6. **EDIT** opens the plugin's native UI; **ON/OFF** bypasses; arrows change order; **×** removes. Changes pause the callback briefly, so do not perform them on-air.
7. With headphones and low hardware volume, click **MONITOR ON**. Confirm audible change between plugin ON/OFF and watch real input/master meters.

## What P2 actually implements
- Windows ASIO/WASAPI using JUCE AudioDeviceManager and native selector.
- Settings persisted to user app-data `AR-MIX-Studio/audio-devices.xml`.
- Native VST3 catalog persisted to `AR-MIX-Studio/plugin-catalog.json`.
- Recursive scan of default/custom folders, direct import of one `.vst3` file/bundle, cached display between sessions.
- Single live microphone/stereo audio path with maximum eight native VST3 inserts, native plugin UI, order, bypass, removal.
- Low-allocation host callback. UI and scanning are outside the callback.

## Explicit exclusions / safety
- **VST2 DLL** is not a VST3 module; raw DLL import is intentionally rejected. VST2 would require a separate, vetted SDK/licensing strategy.
- **Scanning is on a worker thread, not a crash-isolated subprocess**. Broken plugins may still crash or hang the host. This is experimental.
- The other 11 mixer channels and their visual presets are still a concept. Native FX chain applies only to channel 1.
- Real plugin state preset/save/restore, sidechain routing, bridge to OBS and round-trip measurement are not finished.
- VST3 processBlock code belongs to third-party plugins. Host-side callback design alone cannot guarantee zero allocations, x-runs or crashes.
- Plugins that do not support stereo effect layout may be rejected. Missing/invalid libraries are reported to the user.
- Do not infer audio reliability from successful GitHub Actions compilation. Validate with ASIO and WASAPI on physical hardware.

## Acceptance
- GitHub Actions release x64 build GREEN and artifact uploaded.
- Scan actual VST3 installation, catalog populated and survives restart.
- Add/remove/bypass/edit/reorder two real plugins and verify audible routing.
- Save/restart audio device settings and verify the same device is selected.
- Test repeatedly with headphones, 48 kHz / 128 samples then 64 only after stable result.
