# WaveShell identity and startup recovery — P0

Root cause: ArSonMix pins JUCE 8.0.15; its VST3ModuleHandle::create resolves
a class index before IPluginFactory3::setHostContext. Waves WaveShell can
re-enumerate its factory at setHostContext, shifting indices and opening
an entirely different plugin. JUCE fixed this on 2026-09-03 in commit
3fa28d0a14f1be97d49603c710afaa6b9b900cdb.

scripts/patch_juce_vst3_waveshell.py applies the exact necessary ordering
change to pinned JUCE 8 before compilation. Unknown or partial sources
fail CMake configure rather than ship a silently vulnerable VST3 host.
Tests enforce idempotence and host-context-before-class-index ordering.

DescriptionCache::choose now selects by exact catalog product name and
JUCE 32-bit UID, rejecting collisions. Direct ADD, native ChannelId ADD,
legacy and saved-session restore pass the expected name. Catalog keys and
the .smix schema are preserved; a mismatched old catalog fails closed.

Crash-safe startup: Vst3RestoreCrashGuard records automatic saved-FX restore
before instantiating any VST3 vendor module. An abnormal termination leaves
the marker. The next launch opens the GUI and does not repeat automatic
FX restore; saved .smix, preset binaries and factory plugins remain intact.
After 15 seconds of normal post-restore operation, or normal application
shutdown, the marker is cleared. A read-only settings folder fails safe.
User can manually Open after investigating/updating the affected VST3.

This guards crashes within startup FX restore, not all possible startup
crash causes: gather WER dump and machine audio/WebView2 diagnostics.
Physical WaveShell v17 listening, GUI/editor identity and restored sessions
must be tested on a licensed user's machine. This is not physical FAT proof.
