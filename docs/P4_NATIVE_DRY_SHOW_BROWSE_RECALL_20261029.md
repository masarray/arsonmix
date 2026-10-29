# P4 Stage 4 — Native DRY Show Browse & Recall (2026-10-29)

## Product milestone
ArSonMix now has one actual portable Studio/Shows lifecycle for **verified DRY routing snapshots**: SAVE (previous milestone), BROWSE current stored generations, SELECT immutable Show UUID, and RECALL a real device-bound 10 INPUT + 4 AUX + PRE/POST prepared graph. The UI is not localStorage or a fake preview. BROWSE scans at most 256 directories and displays the newest 64 metadata entries. No disk paths are exposed to the WebView. On RECALL the existing worker hashes/loads the complete immutable stored Show generation away from the audio callback and JUCE paint.

After disk IO, the control thread rechecks **device epoch and native route revision** taken before dispatch; changed devices, CLEAR PATCH, concurrent routing APPLY, invalid physical input ports, stereo duplicates, unsupported master state, stale session or third-party VST3 all reject before publication. `NativeDryShowRecall::prepare` is pure control-plane and does not mutate caller command on validation failure. Only the original `LiveAudioEngine::applyDryRoutingLab` can apply the prepared graph with DIRECT and OUTPUT MUTE gates, publisher CAS and RT-safe retirement. On success native MASTER linear gain (including true exact-zero), mute and mono are restored; physical output **remains MUTED** and must be checked by the operator before manually arming.

The existing native 10CH/4AUX processing callback implements MASTER mono with a 64-sample ramp and immediate muted-state priming. No callback allocation, lock, I/O, VST instance creation, graph duplication, per-frame JS repaint or WebView AudioWorklet. The existing CH1 borrowed VST3/Orbit graph is protected; DRY recall rejects any active CH1 FX rack, and stored vendor strip/AUX FX remain unsupported for state/identity safety. It makes **no claim** of port-number equivalence across different computers or audio interfaces.

## Acceptance / anti-regression
- Native CTest exercises actual on-disk Save→load→decode→prepare, exact 10CH sparse physical indices, BUS/NONE, linear zero, CH and AUX sends, PRE/POST, MASTER mono/gain, and failure rollback for unmuted output, lost port, stale native routing, CH1/AUX VST3.
- Chromium smoke covers bounded native browse, rejecting invalid IDs, intentionally selected UUID recall with device READY, no automatic unmute, and visible negative ACK.
- Output-unmute is blocked while Studio/Shows IO runs; panic MUTE remains immediate.
- The same native IO worker serializes SAVE/BROWSE/RECALL, no redundant background threads or CI workflows.
- All merged B3 #160 changes take precedence; port on current main after its exact-head CI, never overwrite the new unpatched production session with an old operator LAB.
- P0 physical 0-VST crackle #15 remains unresolved and requires actual wired ASIO/WASAPI hardware FAT. This DRY functionality is **not** full VST-equipped Show state/portable licensing compatibility, PDC or hardware AUX OUTPUT patching.

## Scope and provenance
Extracted carefully from the older #141 concept, **not bulk merged**. All main/JUCE routing adapters, current approved WebView UI, R2 AUX send matrix, FX shared picker and native graph lifecycle remain source-of-truth. Future full FX-equipped Show recall needs snapshot-safe vendor state capture/restore, instance lifetime, plugin affinity and PDC qualification on hardware.
