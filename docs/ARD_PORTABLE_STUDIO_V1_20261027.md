# ARD — ArSonMix Portable Studio & Disaster Recovery v1.0
**2026-10-27 · issue #85 · STATUS: Approved product intent; implementation phased, hardware FAT pending.**

## 0. Executive contract
**ONE ArSonMix executable, identical capabilities for everyone.** Public installer/portable download contains no user's plugin binaries, personal presets or credentials. Every user may import their own plugins, optionally make a complete portable workspace and back up/restore their sessions after the original computer is lost. Only the **data** differ. Never create an owner-only/pro edition, artificially limit the *catalog* to 3/8/100 plugins, or auto-scan a whole machine merely to load three known VSTs.

Primary journey: `OPEN -> IMPORT 3 VSTs -> SAVE PERFORMANCE -> BACK UP TO USB -> NEW PC -> OPEN/RESTORE -> VALIDATE AUDIO ROUTE -> READY`. On subsequent start, load **only the plugin instances referenced by the chosen Performance**, using a cached index and absolute-path-independent references. A VST vendor may still require redistributables, sample libraries, online license activation or a machine-specific installation: display an actionable *Requires Setup* status; don't bypass activation and don't silently discard states.

**Hard truth about the current native engine**: One live input channel and up to 8 live effect slots, NOT the 10 visual strips' complete DSP. Browser-only localStorage scenes are not native saved sessions yet. The P0-15 zero-VST hardware crackle acceptance is still open. Mark completed milestones separately from future requirements.

## 1. Source audit and constraints (baseline at start: main 1bdabea478e1d1be5a7f83398aa20f5d1f068225)
| Code | As-is | Required |
|---|---|---|
| `src/Main.cpp::settingsDirectory` | AppData `AR-MIX-Studio`, device XML and WebView2 UDF separate | Workspace root for user studio; keep machine audio route preference isolated |
| `src/PluginCatalog.h` | manual .vst3 scan already no global scan; `plugin-catalog.json` absolute paths; no copying | cached indexed relative path; explicit import-to-Vault on background worker; optional load-in-place |
| `src/NativePresetArchive.h` | real .armix ZIP, SHA256 on vendor binary state, atomic overwrite, but absolute `fx.path` | workspace-relative path stored, validated resolution at playback, backward read of v1 |
| `src/LiveAudioEngine.h` | VST3 only; invokes `createPluginInstance` and `setStateInformation` on control path; restore fails on absent absolute path | resolve exact saved plugin identity, controlled preflight and model-based missing plugin, no full scan |
| `web/app.js` | localStorage for UI session/snapshots | native-owned serializable workspace state + migration, do not rely on WebView cache |
| `web/bridge.js` | separate native My Sounds / Plugin Manager | single intuitive IMPORT/SAVE/BACKUP/RESTORE flow using existing approved skin |
| `.github/workflows/p1-windows.yml` | verified native ZIP and exact UI assets | public package starts with empty Studio and native portable marker; no VST binaries or personal data |

## 2. Storage model and mode selection
Same executable; data mode based solely on packaging/user selection:
- **Portable**: ZIP includes zero-byte `portable_mode.txt` beside EXE; native code selects `<exe-dir>/Studio` as settings/preset/catalog root. Both copies in `E:\ArSonMix` and `F:\ArSonMix` become independent workspaces. The folder, not current drive letter, is the sole root of truth. Marker is a data-location switch, **NOT** a feature switch.
- **Installed**: no marker means `%APPDATA%/AR-MIX-Studio` legacy location, preserving current presets and catalog; app may later offer explicit `MOVE TO PORTABLE` migration with transaction/rollback. Do not silently move existing AppData.
- **Machine environment**: active ASIO/WASAPI endpoint settings remain machine-specific and retain current generation and identity fences. Never blindly restore USB-saved driver/device across PC migration; first launch muted and guide the user to select an available device.
- **WebView2 UDF**: distinct per workspace or machine; can be reconstructed, must not be the *only* durable state store. Installed mode preserves current WebView2 directory for backwards compatibility. Cache is not required for backups once browser UI state moves native.
- **Read-only**: refuse operations that would falsely claim save success. Offer normal error and leave original data intact. Never delete a damaged vault/index to 'repair' without user action.

Desired layout (not all dirs implemented in P1):
```
ArSonMix/
  ArSonMix.exe
  portable_mode.txt
  README.md
  Studio/
    plugin-catalog.json             # legacy filename, index v2 later
    presets/*.armix                 # native VST parameter state, validated ZIP
    Plugins/VST3/<identity>/<BundleName>.vst3/   # full bundle or .vst3 file
    Sessions/*.armixsession
    Snapshots/*.armixsnapshot
    Assets/                         # explicitly selected extra resource files
    Backups/                        # verified versioned backup archives
    manifest.json                   # schema/profiles/dependencies, future
    Cache/WebView2/                 # disposable, not authoritative
```
No user's third-party VST or preset data may be included in Git repo/public build; the clean public **portable ZIP has the marker + empty workspace**, still full import and backup features.

## 3. Identity, compatibility and resolution
VST3 module (single file or bundle) has an asset location, format, version, manufacturer, unique VST processor class identity and possibly several processor variants. Path alone is **not** its identity. Future index `PluginRef`:
```json
{"schema":2,"plugins":[
 {"id":"uuid-per-user-plugin-ref","format":"vst3","mode":"managed",
  "relativePath":"Plugins/VST3/<id>/Effect.vst3",
  "moduleFingerprint":"sha256-of-deterministic-tree-content",
  "classUid":"VST3-class-id","variant":"fx-name","version":"x.y","status":"ready"}
]}
```
Logical persisted paths use `workspace://Plugins/VST3/...` (format-neutral resolution envelope), never `C:\` or removable drive letters for managed plugins. Decode requires nonempty valid relative path, rejects `..`, absolute roots, separators not normalized, directory escapes and symlink/reparse traversal where portable materials can be untrusted. A source external to workspace may remain absolute **only when user deliberately chooses Load in Place**, with a clear *not portable yet* badge. Never claim fully backed up in that mode. Exact UID/variant fingerprint of the selected effect resolves first; do not silently fall back to same name/new major version.

No arbitrary catalog entry cap; CPU/memory/device capabilities bound active *instances*. Existing 8-live-FX limit in current engine is a *temporary DSP topology limitation*, not a rule on what may be imported/stored.

## 4. Manual import, fast startup and crash containment
```
Import clicked / drag-drop
   -> Choose VST file, VST3 directory bundle or selected folder
   -> Preview detected modules / optional Copy into Studio vs Load in Place
   -> Worker: preflight free space, boundaries, suffix, symlinks/reparse, architecture
   -> For each selected VST3: stage complete bundle in hidden workspace sibling;
      do not overwrite existing good plugin. Hash and fsync/stage, commit atomically.
   -> Scan metadata for ONLY those selected files, on non-audio thread;
      on success create/update cached manifest transactionally.
   -> UI: '3 plugins ready', optional performance save
```
On startup NEVER invoke `scanDefaults()` automatically. Parse cached index quickly; ask the resolver only for identities the chosen Performance references. No plugin creation or metadata scan in audio callback. Over time move scanner into crash-isolated helper process with deadlines/quarantine for badly behaved vendor code. The existing JUCE Scanner currently runs on a dedicated **thread**, NOT crash-isolated, which must remain clearly visible as a risk.

Staged copy must preserve **entire bundle structure** and `Contents/Resources`. Multiple versions can coexist, no overwriting in-use modules. Changes to plugin binaries generate new version refs. Content hash is used for tamper/corruption proof, *not* permission management.

## 5. Scene/Performance/Snapshot v2 & state
Use `NativePresetArchive` v1 as migration base; it already captures vendor `getStateInformation`, mixer values, ordering, bypass, and SHA256 in `.armix` ZIP. Add `workspace://` reference in persisted `path` while keeping `schemaVersion:1` compatibility at P1. `load()` resolves to current machine absolute path at control layer so unchanged `LiveAudioEngine::restoreSnapshot` can find VST. Reject invalid workspace relative path before touching output or graph. Old absolute-path files must continue to load at their historical location.

Future v2 stores: full plugin class IDs (not JUCE numeric UID alone), plugin version/fingerprint, state blobs, pan/gains/mute/master-mono, graph topology, ten-channel true DSP when implemented, logical routes and unknown-data preservation. Keep output endpoints and activation tokens out of portable preset. UI transient state is not a stand-in for a true native session. VST host must handle plugin state restore on appropriate thread, with safe fade/swap and route/device epoch fence.

## 6. Backup / Restore as intuitive product flows
- **BACKUP STUDIO**: single button opens native OS folder picker; app checks free space, in-use plugin writers, dirty session; creates immutable versioned backup with plugin vault + presets/sessions/snapshots, verifies manifest digests *after write*, then reports `BACKUP VERIFIED`. Separate `CANCEL` leaves prior valid backup unchanged. Exclude rebuildable WebView cache/logs; detect and clearly report external plugins/resources that cannot be included. No license secrets copied.
- **RESTORE STUDIO**: choose archive or folder (or open portable USB directly). Validate manifest/schema/signature-independent checksum, resource paths/size caps, free space; *preview* changes and missing dependencies, keep current workspace untouched until staging is verified; perform atomic switch under stopped graph/muted output, retain rollback. No replacing unknown existing user work without explicit choice.
- **Ready to Perform** is only shown once actual used plugin references resolve, their saved vendor state loads, active engine graph is prepared, and a safe audio device is selected. Distinguish `Ready`, `Needs driver`, `Needs vendor activation`, `Plugin missing`, `State incompatible`. Failed restore never erases the only good copy.
- **User-visible UX contract**: same buttons for every user, no mode-specific rights: `ADD VST` → `SAVE PERFORMANCE` → `BACKUP STUDIO`; `RESTORE STUDIO` to recover. Advanced full scan optional and manual, never prerequisite.

## 7. RT, performance, safety and failure semantics
- No file IO, hashing, logging to disk, JSON parse, COM calls, VST instantiate, scan, copy, alloc or locks added in the JUCE audio callback. No extra FIFO on the verified ASIO DIRECT single-clock path. Prestage all plugin state and graph on control executor and hand off atomically with bounded generation-aware logic.
- USB loss while running: do not access freed memory; stop writing, preserve in-memory state where safe, mute/fail-closed on indispensable resource stream failures. A running plugin may itself read files from USB; portable storage availability is a vendor dependency and **cannot be universally guaranteed**. Warn when source drive is removable; recommend staging workload on SSD for critical live usage.
- Guard unsafe paths: ZIP slip, `..`, NTFS reparse points/symlinks, nested bundle escaping root, alternate streams, DLL search ordering, false .vst3 extension, reserved Windows names, disk full, interrupted write, race between scan and export. Fail safe with repair message, not silent plugin substitution. Strongly prefer out-of-process scanner for untrusted plugin binaries.
- Enforce file hashing, readback, transactional `TemporaryFile` and versioned manifest, no global startup scans, no arbitrary vendor allowlist. Protect against plugin dependency on external licensing drivers and files. Do not bypass vendor activation. Bundle binaries into user-created exports only at explicit user request; never place them in official public GitHub artifacts by default.

## 8. Milestones / actual implementation status
| Gate | Deliverable | Pass evidence | State |
|---|---|---|---|
| **P1** | portable marker root, unchanged installed AppData, catalog cached-path rebase, `.armix` relative path roundtrip, clean ZIP marker | 24 existing CTests + new deterministic relocation test + Windows ZIP assertions + Chromium | **IN DEVELOPMENT: this PR** |
| P2 | select-only import COPY full bundle to Vault, staged atomic copy, indexed plugin variants/version; load-in-place alternative | 3 chosen VST files, no full scan, USB relocation, corrupt/reparse test, Windows CI | pending |
| P3 | performance 'save & quick open' indexed plugin loads, no rescan at startup, missing-plugin recovery | restart 3 chosen plugins after cold reboot, exact state and async graph | pending |
| P4 | native-backed scene/snapshot/session, mono and multi-channel once truly implemented, migration from browser localStorage | roundtrip/migration data preservation | pending |
| P5 | backup/restore button UI, manifests, safe archive export/import and readback, dry-run preview, corruption rollback | cross-PC USB full recovery matrix | pending |
| P6 | public installer and portable ZIP with same feature set, runtime prerequisites, no included user VST | downloaded artifact verification | pending |
| FAT | actual user device, vendor VST3 activation, 0-FX crackle and full profile switching | physical listen/signal metrics, no speculative success | pending |

Each gate is a small diff against latest main. CI spending bounded: reuse current Windows MSVC Release / CTest / Chromium workflows, do not add extra runner for every test. Concurrent GUI owner changes must be respected; engine changes must never replace approved `web/app.js`, `web/styles.css` or standalone UI from old branches.

## 9. Test matrix and definition of done
1. Same build, two users with different bundles; all controls/features identical.
2. No marker on installed system: same pre-existing AppData path, no data loss. With marker in EXE dir: `Studio/` becomes authoritative, no dependence on original AppData; verify no writes to wrong directory except machine audio environment.
3. Save preset with a `workspace://` local VST3, move full folder from `D:\` to `F:\`, load preset: resolve identical plugin path **under F:**, not old D:.
4. Evil manifest `workspace://../outside.vst3`, drive-letter injection, mixed backslash, malformed schema: explicit load refusal without modifying prior valid snapshot.
5. Save/read old absolute-path `.armix` v1, compatibility retained; reject missing plugin without erasing preset.
6. Third-party full bundle/resources copied atomically with preserved file names; two versions coexist; symlink/pseudo-DLL injection refused.
7. Select three plugins and restart: **zero** `scanDefaults` calls; only three prepared instances; debug logs distinguish catalog index load vs metadata rescans. Substantial sample library/activation dependency produces actionable warning.
8. Public ZIP contains no VST3 or user snapshots; portable marker present; verify exact shipped Windows GUI with source-resolved build provenance. USB removal and read-only staging do not destroy existing workspace.
9. Native vendor states 1KB/1MB/8MB, wrong-state/rejected ZIP, snapshot rename collision, drive swap, 50 repeated backups/restore, no corruption; actual hardware FAT remains separate.
10. Startup and event timing measured at 3/30/300 indexed plugins, budgets assigned from real hardware data, **no** arbitrary catalog truncation.

## 10. Sources checked
- Steinberg VST3 [app-local plugin locations](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BLocations.html) and [bundle layout](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BFormat.html): Windows VST3 can be app-local; a modern bundle has resources and architecture-specific binary. App-local **does not imply scan every folder at start**.
- JUCE [plugin format scan APIs](https://docs.juce.com/master/classjuce_1_1VSTPluginFormat.html) and [File copy](https://docs.juce.com/develop/classjuce_1_1File.html): specific file inspection and recursive copying semantics.
- OBS [portable mode](https://obsproject.com/kb/portable-mode): same executable with portable marker, note OBS does not relocate media paths; ArSonMix MUST do better.
- Microsoft [WebView2 user-data folder](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/user-data-folder): cache isn't the authoritative source of app data.
- [Project product contract #85](https://github.com/masarray/arsonmix/issues/85), [P0 hardware FAT #15](https://github.com/masarray/arsonmix/issues/15), [native smart audio blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md).

**ARD change-control:** alter these invariants only with an evidence-backed architecture decision; never mistake a prose milestone table for tested native functionality.
