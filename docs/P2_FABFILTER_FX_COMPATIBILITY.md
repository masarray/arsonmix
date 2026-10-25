# P2.4.1 — VST3 Add FX compatibility and failure diagnostics

Hardware report: FabFilter plugins appear in installed VST3 catalog, but pressing ADD has no visible effect.

## Source audit and fixes
- Confirmed UX defect: Native engine previously emits loading errors as a toast obscured by the Add VST3 modal; a failed insert looks like no response.
- Confirmed compatibility restriction: the host always called `setBusesLayout` with exactly one stereo input and one stereo output, potentially rejecting VST3s with an auxiliary sidechain bus or already valid default layout. Now it preserves the plugin's existing compatible main stereo bus layout, or negotiates stereo main with optional auxiliary buses disabled.
- Plugin creation still runs synchronously in the UI message thread; an indeterminate "Loading…" indicator is painted before calling JUCE, but long/blocked third-party VST creation remains possible.
- New native result event `arMixAddResult` sends result/error, elapsed time, request ID and chain size. Failures stay visible **inside** the Add FX dialog, with a diagnostic log event `FX_LOAD_FAIL`.
- Both adding a plugin and restoring a native preset apply the same bus negotiation.

## Trial
1. Use the new Windows artifact in PR; open **MIX → ADD FX**, search FAB.
2. Choose FabFilter Pro-C 2, Pro-Q or Pro-DS. Observe Loading… status, then insert success or visible exact error.
3. On success, the plugin appears in Channel 1 live rack; click its name to open native UI; toggle power to bypass. Test with headphones at low volume.
4. On failure, copy the full text of the red FX NOT LOADED message and open **SIGNAL CHECK → OPEN DIAGNOSTIC LOG**. Attach log for class/bus/plugin-specific diagnosis.
5. Test at least one previously working ArSonKuPik and native preset Save/Recall to prevent regression.

## Remaining risks
- No real FabFilter VST3 binary and license is available to CI. Windows build GREEN does **not** prove vendor-specific runtime compatibility.
- Creating VSTs remains synchronous on the host message thread, so slow initialization may stall even with a Loading state. True off-thread staging plus graph swap is subsequent ARD work.
- Scanner is a worker thread, not a separate crash-isolated process.
- Host processes exactly one native stereo Channel 1 path; other mixer strips remain previews.
