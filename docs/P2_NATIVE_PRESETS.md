# P2.4 — Native My Sounds · VST3 binary preset ZIP

**Source of requirements:** `docs/ARD_AR_MIX_LIVE_FX_HOST_v0.3.md`, sections 2, 7, 8, 8.1, 13. This phase closes the fundamental persistence gap. This is NOT yet complete session/snapshot support or live-safe hot-swap.

## Completed milestone
- A native `MY SOUNDS` button is available beside Live FX Rack and in the FX Rack toolbar.
- Save `*.armix` files to the native app-data `AR-MIX-Studio/presets/` directory.
- ZIP contains `manifest.json` and `state/00.bin` through `state/07.bin`, captured with JUCE `AudioPluginInstance::getStateInformation` and restored with `setStateInformation` (full vendor-controlled binary state, unlike browser dummy knobs).
- Manifest includes schema/version, Channel 1 input gain/PAN/mute, master gain/mute, ordered stable plugin file/UID/catalog identity, bypass and SHA-256 per plugin-state blob. Output device/driver routing is **not** stored or switched.
- Input validates 8-plugin maximum, 8 MiB per plugin state, ZIP file 72 MiB, 256 KiB manifest, fixed entry names/entry count, finite numeric settings, plugin identity and SHA-256. It never extracts archive entries to arbitrary filesystem paths.
- Recall stages a full new chain and restores each VST3 state before detaching the existing audio callback. If plugin validation/creation/state restoration fails, active chain is left intact. Old processors are retired on the message thread after callback is detached.
- Audio output **must be muted before save or recall** because vendor plugin binary state calls and current graph detach are not guaranteed glitch-free. Output remains muted after recall. The UI explains why instead of silently interrupting a live broadcast.

## Physical trial on Windows
1. Select input/output in AUDIO SETUP and add ArSonKuPik or another known working VST3. Make an audible adjustment inside the plugin editor.
2. Open SIGNAL CHECK and click MUTE OUTPUT. Open MY SOUNDS, enter `Vocal Test 1`, SAVE SOUND.
3. Edit the plugin parameters to something clearly different or remove the plugin while muted.
4. Open MY SOUNDS and RECALL the saved sound. Check plugin editor state, bypass and processing, fader/PAN/master settings, and insert order. Unmute output manually.
5. Close/restart the EXE and repeat recall. Native presets should persist. Confirm there is no unexpected audio-device switch.
6. Test corrupted archives and missing plugins. The current chain must be retained when validation fails.

## Deferred ARD work / phase gate
- Smooth live graph switching with a prepared immutable graph + epoch-ack and bounded crossfade, so snapshot changes can be auditioned without output mute.
- Full mixer snapshots including every native channel and sends, A/B preview, durable sessions and autosave journal.
- Dedicated storage worker and out-of-process plugin scanner. ZIP save/restore I/O and vendor plugin preparation currently run on the non-real-time message thread *while output is muted*.
- Multiple independent physical/virtual inputs and real routing/stream output, rather than demo strips.
- Plugin compatibility test matrix / actual Windows xrun & 60-minute soak gates.
