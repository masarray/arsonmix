# How to use ArSonMix Portable Studio — initial working slice (P1 + P2)

**Same ArSonMix for everyone.** No separate owner/pro edition. This guide describes **P1/P2 scope only**; the future one-click BACKUP/RESTORE, complete ten-channel DSP and automatic session migration are NOT yet delivered.

## Prepare once
1. Download **verified Windows portable ZIP**, extract it in a writable directory or USB drive (not inside protected Program Files). Keep `ArSonMix-GUI10CH-P012-Windows-x64.exe` and `portable_mode.txt` side by side. That file is **not** a license/feature toggle; it selects local `Studio/` storage.
2. Run EXE. Under **PLUGIN MANAGER**, click **ADD VST · KEEP IN STUDIO** and choose the exact `.vst3` you use. It may be an original single-file `.vst3` or a folder bundle ending in `.vst3`. ArSonMix copies a full bundle, including `Contents` and `Resources`, to `Studio/Plugins/VST3/<unique>/` in a background scanner. It inspects only your selected plugin; there is no need to click **SCAN SYSTEM** for three chosen VSTs. Repeat only for the VSTs you actually use.
3. Optional: **LOAD IN PLACE** references a plugin's original path without copying. This is flexible but **not fully portable**; make a managed copy before a disaster-recovery backup.
4. Select the copied plugin on **Channel 1 live FX chain**. The current native audio engine supports *one actual native input channel* and up to eight effects, irrespective of how many entries the user stores in their library. Save the real vendor VST state through **MY SOUNDS → SAVE SOUND** after muting output. This creates a native `.armix` in `Studio/presets/`, with portable `workspace://` identifiers.

## Basic manual backup / restore until the P5 one-click UX lands
5. Close ArSonMix completely, confirm there is no ongoing plugin import. Copy **the entire ArSonMix folder**, including hidden folders and `Studio/`, to a USB drive, using Windows Explorer. Safely eject the USB. Keeping a *second* copy on another drive guards against losing the flashdisk itself.
6. On the replacement Windows PC, copy the folder to a writable location (or execute from USB), run the same EXE, and select available **AUDIO SETUP** driver/routing with output muted until verified. Then open **MY SOUNDS** and **RECALL LIVE**. Managed VST3 paths are computed relative to the local `Studio/`, so old `D:\` or `F:\` letters are not used for those plugins. No global VST scan should be needed to reopen the cached managed catalog.
7. If a vendor requires an additional system runtime, license activation, extra sample library or registry installation, fulfill those documented vendor requirements on the new PC. Copying a binary is not a licensing bypass and cannot guarantee all commercial plugins are immediately operable. An old **LOAD IN PLACE** path still needs the external file.
8. **Important:** Browser-only cosmetic session/snapshot `localStorage` has not yet been migrated to native portable files in P1/P2. The native `.armix` plugin FX state is backed up as described; do not assume every GUI-only scene is fully disaster recoverable until P4. A complete user-friendly backed-up session is the P5/P6 release gate, NOT an already shipped feature.

## Scope and operational safeguards
- VST3 current only. Native instruments, MIDI, VST2, CLAP and genuine 10-channel DSP need separate format/routing adapter work. Plugin library size is independent from the current live rack capacity.
- Source plugins are **not deleted or overwritten**. New imports go into distinct Vault locations. A directory of unrelated plugins is never scanned implicitly on startup.
- Current scanner is on a worker **thread**, not yet a crash-isolated process; untrusted vendor code remains a stability risk. Avoid copy/import during critical live performances, and only use trusted plugin binaries.
- No disk scans/copies inside the host realtime callback, no changes to ASIO DIRECT buffer path. Physical P0 zero-FX crackling FAT remains open under GitHub #15.
- Later **BACKUP STUDIO/RESTORE STUDIO** will create verified manifests/hashes and perform transactional recovery with non-destructive preview; manual Windows folder copies are an interim workflow.
