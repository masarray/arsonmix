# ArSonMix — Shows, Snapshots, Safe Recall (2026-10-28)

## Goal and grounded baseline

Make left-rail recall as convenient as Soundcraft Ui's Show/Snapshot workflow, **without pretending a WebView visual snapshot is a real native audio state**. A Show contains named Snapshots. Fast selection in the left rail, accessible management from the current snapshot label, and optional per-channel Recall Safes. A Snapshot can restore an entire mix; Safes preserve selected live channels.

Current code audited:
- `web/app.js`: browser/UI-only scene state + localStorage JSON. This change adds a small manager for Save New, Update, Recall, Rename, Delete, Recall Safes, last-state Undo, clear native unsupported notice. Nothing here proves a VST3 state transition.
- `src/NativePresetArchive.h` + `Main.cpp`: real native .armix save/restore for a narrower CH1 FX chain, with VST3 binary state and validation.
- `docs/P1H_NATIVE_MULTICHANNEL_VST3_20261028.md`: up to 10 separate native channel FX processors now exist in opt-in LAB, but 10CH Show/Snapshot serialization and safe live atomic recall have **not** shipped.
- `src/StudioBackup.h`: vault and native data backup exists, but not a completed multi-Show transactional store.

## UX contract

| Entry | Behavior |
| --- | --- |
| Left rail snapshot row | Quick select/recall, highlights loaded item, visibly identifies factory/user |
| + SAVE SNAPSHOT | Opens compact manager, name, selection, safes, action grid |
| SAVE NEW | Unique name, never overwrites existing snapshot |
| UPDATE | Two-click confirmation, changes selected user snapshot only |
| RENAME | Keeps snapshot content and stable selected item, rejects duplicate names |
| DELETE | Two-click confirmation, factory entries immutable |
| RECALL | Applies recorded state but preserves current state of Safe channels |
| UNDO RECALL | Restores previous mix in preview; future native version must be transactional |
| Header snapshot selector | Opens same manager, keyboard-operable |
| Native mode before final engine support | **Explicitly refuses fake UI-only snapshot writes/recalls**, points user to native MY SOUNDS |

Do not auto-switch driver, sample rate, buffer, physical ports, mute policy, capture/render clocks or ASIO devices when recalling a Show or Snapshot. Hardware configuration is a separate reviewed action.

## Native implementation (next gated milestone, not marked done)

### 1. State model and stable identity

`ShowV2 {id, name, schema=2, channelSafety, snapshotOrder, currentSnapshotId, created, updated}`.
`SnapshotV2 {id, name, created, modified, sourceBuild, channelStates, masterState, busStates, pluginRefs, pluginStateHashes}`.

Every one of the 10 channel records uses a stable channel ID, plus fader, gain/trim, PAN, mute, solo, polarity/HPF, stereo/mono port contract, FX ordered identities (path in managed vault + plugin UID + binary fingerprint + bypass), sends and bus state. Master record contains its own gain, mute, mono/stereo and FX state. Only opt-in routing states that can actually execute in native P1H/P1G may be declared persisted; no phantom GUI FX processors. Full vendor VST3 getStateInformation/setStateInformation blobs must be round-tripped intact.

### 2. Storage and lifecycle

Managed portable workspace path: `shows/<showId>/manifest.json`, `snapshots/<snapshotId>.json`, immutable `blobs/<sha256>.bin` references; no vendor plugin redistribution. Restrict file count/size, validate UUID/name/path, use JSON schema version + migrations, checksum and byte cap per plugin and show. Write on worker/control thread: stage all blobs and manifest in same-volume temporary files, flush, rename atomically, retain previous generation and recovery journal. Corrupt/missing plugin archives leave the prior snapshot and active graph intact. Failed Save never replaces the last known good session. Make all existing shows included in Studio Backup/Restore.

### 3. Safe native recall pipeline

1. Prepare immutable recall plan off audio callback. Resolve all 10 native VST3 identities from the trusted catalog/vault and validate ports, buses, CPU/latency and settings.
2. Freeze mapping of channel Safes, currently selected physical device/ports and driver generation. Apply Safes to **stable channel IDs**, not GUI strip positions.
3. Instantiate/processors, restore binary vendor state and prepare 10CH DSP + buses on the control thread. If any required plugin is missing/incompatible, show exact channel/slot error and **do not publish a partial state**.
4. Publish exactly one new generation to the audio callback (immutable pointer/generation or existing graph handoff protocol). Never hold file, allocation, parsing, plugin state operations or mutex on the realtime thread.
5. At a callback boundary, de-click output with a bounded documented fade as required by graph semantics, retain old generation until acknowledged and safely retired. Respect Orbit and bypass envelope state transitions; do not regress P0 anti-crackle.
6. Only after native acknowledgment: update the WebView "current snapshot", rebind fader/PAN/mute/FX and record event. On timeout/failure recover prior graph; no "Saved" or "Recalled" success while pending.

**Live-safety policy:** current CH1 native MY SOUNDS operations may require a muted output. The new 10CH pipeline must not copy/paste that blocking audio path into live performance; gate any non-RT-safe vendor plugin behavior and visibly offer MUTE/RETRY instead.

### 4. Tests and release gates

- Unit: schema/manifest validation, safe mappings across reorder, duplicate/rename/delete, atomic file failure rollback, multi-snapshot show migration, missing/corrupt VST3 blob.
- Native CTest: 10 unique plugin identities; successful and failed prepare; generation ownership; one logical commit; bypass envelope and DearVR Orbit association preserved; repeated recall does not leak/copy callbacks; 10CH send/master state assertions.
- UI: 11 strips geometry unchanged; keyboard accessibility; left rail list; correct "pending/failure/success" state; no fake native success. Snapshot managers do not run in meter hot-path or create periodic timers.
- Hardware FAT: real Windows WASAPI/ASIO, multi-device, buffer 128/256/480, plugin editor open, 60-minute idle/active switching, audible crackle/click and xrun monitoring on onboard graphics. Compare old/new session after restart, unplug/replug and backup on a new PC.

### 5. Milestone order

- **P0 done in this PR:** correct toolbar OFF/ON and lightweight hover; stabilize JUCE Audio Setup initial viewport; preview manager CRUD + safes/recovery; explicit native guard.
- **P1 next:** native ShowV2/10CH SnapshotV2 storage, disk worker and signed runtime source-of-truth; portable backups, round-trip all 10 plugin blobs.
- **P2 next:** native transactional immutable recall with ack/de-click, SAFES and full live UI binding; tests and device FAT.
- **P3:** A/B audition, snapshot differences, Show import/export/merge with conflict preview and per-Show undo history.

No PR should be marked production-ready merely because HTML preview values persist to localStorage.
