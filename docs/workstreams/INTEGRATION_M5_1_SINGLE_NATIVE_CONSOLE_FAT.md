# M5.1 — Native M4 physical GUI/ENGINE FAT (joined exact trees)

ENGINE parent: `9d0ecabbdd3ba2b1629a8c3917ee64e0b6ae139d` from draft PR #266.
GUI inputs: `67ef6e7b0afab173840e611b7fdf6627a96e714a` (31 exact files from draft PR #271).
No files or refs were written to GUI #271. GUI visual styles/assets are copied byte-identically; only integrated `web/mixer-layout.js` adds a clearly namespaced M4 control adapter.

## Physical control contract
- The one JUCE audio callback still renders the genuinely accepted 10CH + 4AUX M4 graph. No additional audio callback, DSP chain, WebAudio route or second FX owner.
- ENGINE `legacyM4.controlPlane=LEGACY_M4_ORDINAL` requires connected hardware, an admitted 10-strip route and a healthy physical topology. Missing authority means **no controls**.
- UI validates the native schema v6 snapshot first, then creates *presentation-only* `M4:1`–`M4:10`, enabled AUX and pinned MAIN strips. These are never native permanent ChannelIds. `productionCallbackLive=false` and the real native `graph.channels=[]` / `capabilities.supportedOps=[]` remain unchanged on the wire.
- M4 FADER/PAN/MUTE/SOLO, verified physical input selection, MAIN/NONE patch and existing native VST3 bypass/editor and AUX send gain emit the unchanged `arMixConsoleIntent` with **decimal ordinal target**, **deviceEpoch**, **baseGraphRevision/baseRosterRevision**, and **legacyRoutingRevision**. Native rejects invalid/stale/unsupported commands; acceptance uses native ACK then exact refreshed snapshot.
- Native routing revision/topology loss invalidates pending UI gesture locks, no queued gesture replay across RE-APPLY/hotplug. Latest fader/PAN gestures are coalesced by the installed atomic target on the control thread.
- M4 GUI shows actual physical PCM status only; CTest/Playwright are *not* hardware acoustic verification.

## One Windows executable, no dead assets
`CMakeLists.txt` and `src/Main.cpp` now embed/serve **all** HTML-referenced JS (including GUI `hardware-keys.js`) and the four actual PNG asset URLs referenced by `mixer-layout.css`. `BUILD_PROVENANCE.json` and independent ZIP verifier attest exact full embedded source of all JS and CSS. New architecture gate checks HTML/CSS URL ↔ BinaryData ↔ WebView2 provider parity.

## Test order
1. Ubuntu/Chromium: legacy native snapshot → 10CH/MAIN display → MUTE intent (real ordinal, legacy revision) → ACK → refreshed native MUTE → PAN and invalid-authority fail closed; existing universal GUI fixtures must retain proper behavior.
2. Windows MSVC: exact-SHA build/CTest including 200k coherent control gestures, 20k overlapping render permits, native old M4→GUI socket and original VST3 identity.
3. Independent Windows PE/WebView2 embedded resources and ZIP byte attestations.
4. **NOT TESTED** until an operator runs real hardware: low-volume wired 0FX output-only/capture-only/dry-through (issue #15), correct MUTE→silence, 1FX vendor editor, PRE/POST AUX, Show and device rearm. Bluetooth/cross-clock quality is not inferred.

This is the M4 physical compatibility vertical slice and **not** the universal ChannelId production cutover; Group/FX_RETURN creation remains disabled until real one-callback admission/parity and hardware FAT.
