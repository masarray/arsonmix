# Portable Studio P3 — verified one-click native backup (2026-10-28)

## What users get now
**All ArSonMix users have exactly the same feature set.** On the current GUI, open **MY SOUNDS / SAVE / RECALL**, first save the actual native VST3 performance as a `.armix` sound, then **MUTE OUTPUT** and click **BACKUP STUDIO TO USB / FOLDER**. Choose a writable USB drive or directory. ArSonMix creates **a brand-new folder** `ArSonMix-Backup-YYYYMMDD-HHMMSS-ID` containing:
- copy of the **running Windows GUI EXE**, portable startup marker and all actual native `Studio/` presets, catalog, Plugin Vault VST3 files/bundles, and user-selected assets
- `backup-manifest.json` recording schema, relative file names, sizes and SHA256 digests
- **no** WebView cache or diagnostics logs, which are not the saved plugin state

Before publishing the backup, it reopens and verifies every file's SHA and size and audits for unlisted or linked paths. The directory is staged and renamed into place only when verified. Failed/partial copies delete their hidden staging location and **never overwrite an older valid backup**. The GUI reports actionable success/failure; copies/hashes run on a non-audio worker and the backup button requires **output muted** to avoid disk contention during live performances.

If the native plugin catalog contains paths still outside `Studio` (the freely available **LOAD IN PLACE** workflow), the GUI reports the number of non-portable plugin references. `ADD VST · KEEP IN STUDIO` is the recommended workflow for users who want their VST3 binaries on USB. Full system scan is never required for three manually selected VST3s.

## Using your backup on another computer (current intermediate protocol)
Close ArSonMix, eject USB safely, then on the destination PC copy the verified `ArSonMix-Backup-...` folder to a writable local SSD or run from a sufficiently fast drive. Open the EXE inside; `portable_mode.txt` chooses the adjacent `Studio/` directory. Your managed plugin index and `.armix` binary states are referenced by workspace-relative IDs, independent of drive letter. In **AUDIO SETUP**, verify and select that PC's driver and physical outputs before unmuting.

**Do not treat this P3 backup as guaranteed full computer-disaster recovery yet**: some UI-only snapshots still live in browser localStorage and do not travel with this backup; old external LOAD IN PLACE references, vendor sample libraries, VST3 activation/driver prerequisites, and physical ASIO interfaces may need separately restoring. Saved native `.armix` FX states are included. The current DSP is still one real Channel 1; ten GUI strips are not equivalent to ten independent actual VST hosts. The live output crackle hardware FAT remains open (#15).

## Engineering boundaries and follow-on phases
- No filesystem work, SHA calculation, COM call, VST construction, scan, or allocation added to audio callback; no ASIO DIRECT extra FIFO. Code: `src/StudioBackup.h`, integration in `src/Main.cpp`, UI in `web/bridge.js`.
- Only explicitly selected workspace and EXE are copied. No blanket Installed VST folder enumeration, no third-party DRM bypass, no upload to GitHub public release.
- Staging and commit happen under destination root. Native tests simulate backup, corruption, unlisted payload and two separately versioned backups. CI reuses existing Windows CTest and Chromium jobs.
- The SHA256 manifest detects accidental modifications, but it is **not digitally signed**, so a malicious actor who can alter both data and manifest could forge it. Do not execute untrusted VST3 files. Junction/reparse points are rejected from Studio and manifest paths are workspace-relative.
- P4 must move all legitimate GUI scene/snapshot/performance data from WebView localStorage to native workspace persistence. P5 must add a verified, non-destructive **RESTORE STUDIO** import/preflight, error handling, session migration, and rollback with device safe routing. P6 cross-machine real vendor VST3 FAT and public clean distribution proof. **Neither P4 nor P5 is implemented by this PR.**
- Do not force users to install a private edition; there is exactly one app for all users.

Repository reference: [Portable Studio Product Contract #85](https://github.com/masarray/arsonmix/issues/85), [ARD Portable Studio v1](ARD_PORTABLE_STUDIO_V1_20261027.md).
