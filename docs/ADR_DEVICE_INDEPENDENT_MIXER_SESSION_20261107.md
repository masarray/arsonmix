# ADR — ArSonMix Device-Independent Mixer Session & Crash-Safe Resume

**Owner correction 2026-11-09 — regular-user / player first sound:** [Smart Zero-Friction Operator Workflow](ADR_ARSON_PLAYER_ZERO_FRICTION_SMART_WORKFLOW_20261109.md) supersedes any sentence below requiring an extra user ARM, MONITOR ON/OFF, Apply Graph, route confirmation checkbox or repeated Start **after a fresh explicit Play on an exactly matching previously verified physical output**. The Play gesture itself is the operator's requested activation; prepare/restore must be an internal transaction with real native readback. Do not autoplay on launch, auto-unmute MAIN, silently swap to new physical output, or override intentional SOURCE ONLY/BUS. A genuinely unknown/different device requires one contextual output selection; all identity and PCM safety protections remain binding. This is a product policy for future implementation, not evidence the engine currently satisfies it.
**Status: IMPLEMENTATION DESIGN / NOT YET IMPLEMENTED**
**Date:** 2026-11-07
**Canonical integration:** PR #418, preserve ONE JUCE audio callback, native ChannelId PCM ownership, and the single assembled `/preview/` GUI.
**Driver incident:** Operator confirms WASAPI works; ASIO4ALL was reconfigured from existing WDM inputs to VB-Cable capture + Realtek playback and the host showed `Current device is not open; refresh`. Software replay is not physical qualification.

## 0. Non-negotiable user outcomes

1. Device selection, driver sample-rate/buffer, pin setup, and driver loss **NEVER erase mixer content**: channel IDs/order/names, channel format, faders/PAN/MUTE/SOLO, EQ and builtin effects, VST3 chains/slot identities/params/bypass/order and available saved binary states, groups/AUX/MON/MATRIX/CUE that are actually supported, sends, WIRE edges/gains, bus/master settings, routing intents, and GUI user workflow. Maintain EXACT values, not defaults.
2. Opening an Audio I/O window without committing hardware change MUST NOT mutate, discard, re-create, or reset the accepted mixer graph.
3. A different hardware device can invalidate PHYSICAL endpoint bindings only. Keep every mixer control and internal edge intact. Mark unresolved physical IN and OUT as **NEEDS REMAP**. Do not infer that "ASIO4ALL v2" on both selectors proves VB-Cable IN or Realtek OUT; require current driver port readback and identity evidence.
4. On restart, reopen the **last verified mixer document** immediately, even if no device can open; the mixer looks as previously used, but all hardware outputs remain DISARMED until a fresh verified device, routing and explicit user ARM.
5. Named user snapshots/Shows and "Latest Session" auto recovery are distinct features: users can save immutable presets and return to them, while autosave is an unobtrusive crash-safe last-known-good working state.
6. No extra callback, JS/WebAudio DSP, audio-thread disk I/O, graph rebuild on GUI navigation, optimistic ACK, automatic output routing or auto-unmute. Preserve existing `/preview/` geometry and approved icon.

## 1. Audit findings, current source (PR #418 at design time)

- `src/LiveAudioEngine.h::beginManualAudioSetup()` calls `dryRoutingLab_.disable()`; `finishManualAudioSetup()` invokes `startUnpatchedProductionSession()`. `LiveDryRoutingLab::startUnpatchedSession()` explicitly creates ten NONE sources, gains 0.25, neutral pans and zero sends: a real native mixer reset.
- `src/Main.cpp::showDeviceSetup()` invokes `beginManualAudioSetup()` **on opening the native JUCE settings window**, and calls `finishManualAudioSetup()` when closed; `applyCustomAudioDevice()` uses the same reset machinery. Both entrypoints must be transactionally unified. Merely closing a settings window must not replace a session.
- `LiveDryRoutingLab::prepareDevice/deviceStopped` increment a *transient* device epoch and revoke valid hardware masks. This is correct for hardware safety, but NOT evidence that the user-authored mixer document should be thrown away.
- `web/approved-native-bridge.js::refreshRealM4Inputs()` may reset presented input to None when native authoritative state is unpatched; UI must preserve document draft and display `NEEDS REMAP` without falsely claiming active PCM.
- `NativeLiveShowCapture`/`NativeDryShowRecall`/`NativeShowSchema` and `NativeShowStore` already offer useful transactional snapshot, checksum and immutable HEAD primitives. Their current fixed 10 CH + 4 AUX schema does NOT losslessly represent today's dynamic ChannelId GROUP/FX RETURN/WIRE graph. Do not repurpose this restricted format as full-session autosave or silently drop extra buses.
- `StudioBackup` is a user-directed whole-Studio export with integrity manifest. It is NOT startup autosave.
- Windows FAT run 42318730495: MSVC builds, CTest **58/58 PASS**, package fails due to a different `favicon.ico` hash during `scripts/package_native_windows.ps1` regeneration. CMake-selected Python is recorded in `build/approved-gui-python.txt`, while the packager independently invoked PATH `python`; the optional final verification script made the same mistake. Preserve SHA guard and use the exact CMake interpreter at BOTH boundaries. The generated current branding cannot be considered delivered until ZIP + embedded PE icon checks pass.

## 2. Architecture — two lifetimes, one native authority

```text
                  UI /preview/ — draft gestures and commands only
                                |
                          Native CONTROL owner
                                |
                 MixerSessionStore / SessionDocument
            stable ChannelId / BusId / EdgeId / FxSlotId / revision
               /                  |                     \
     Live immutable PCM      DeviceBinding          Persistence I/O
     graph + FX leases       (runtime epoch)         .stage + HEAD + LKG
     one JUCE callback       masks / ports           snapshots + autosave
               \                  /
             DeviceApplyCoordinator (one serialized transaction)
                    |
                 ONE JUCE AudioDeviceManager
```

- **MixerSessionDocument (durable)**: stable logical identity + channels, buses, WIRE edges, matrix/group sends, source *intent*, FX chain references and bypass, channel controls, EQ states, master controls, UI workspace hints, named scenes. Does not embed a driver object, device epoch, callback pointers, realtime counters, screen DOM state or "output armed" permission.
- **AudioDeviceProfile (durable but independent)**: chosen backend and registered driver identity, separate endpoint identity where actually available, preferred sample rate and period, preferred physical pin roles, optional compatible hardware profile. The profile is a *preference*, not proof an output is safe or connected.
- **DeviceBinding (ephemeral)**: current JUCE device instance, verified sparse physical pin masks/labels, actual rate/period, callback generation/epoch, authorization, active mappings, failure causes and monitoring armed state. It may become unbound without modifying the mixer document.
- **PreparedRealtimeGraph (ephemeral/rebuildable)**: prepared render graph, channel buffers, plugin runtime instances/leases and PDC for current device format. Re-preparation on rate/buffer/port change is permitted, but the serialized user mixer state remains immutable and can rebuild exactly on a compatible device.
- **SessionStore** owns accepted model. All UI readbacks and effect commands derive from accepted native state. A device ACK does not declare restored routing LIVE; a later native graph generation/readback is required. Control, device and document revisions are separately monotonic; do not copy temporary device epochs into serialized IDs.

## 3. DeviceApplyCoordinator — two-phase, idempotent, silence-safe

**Before any mutation:**
- Gate one transaction by requestId, expected document revision, device generation and pending FX/Show work. Capture/pin an immutable **accepted MixerSessionDocument** and references to existing native VST3 leases; do not stop or erase logical routing. A device chooser opening should display current values without entering this state.
- Preflight current output state. Require explicit user review and disarm before hardware reassignment. Keep the single direct output safety fence; disarm secondary stream. Never secretly switch from ASIO to WASAPI, another speaker, or an arbitrary first enumerated port.
- Capture a non-RT complete safe checkpoint. For VST3 states that cannot be captured safely while live, retain the last verified state blob and mark it **DIRTY / SAVE PENDING**; never claim a lossless vendor snapshot. Do not block the callback for disk/JSON/plugin serialization.

**Apply**:
```text
IDLE -> STAGE_DOCUMENT -> QUIESCE_OUTPUT -> OPEN_NEW_DEVICE
    -> VERIFY_IDENTITY + PIN_ROLES + RATE/BUFFER -> PREPARE_NEW_GRAPH
    -> VALIDATE_DOCUMENT + BIND_HARDWARE -> ATOMIC_GRAPH_SWAP
    -> READBACK -> ACCEPTED_MUTED (or NEEDS_REMAP_MUTED)

failure: OLD_DOCUMENT_INTACT -> attempt verified old-device restore once
    -> RESTORED_MUTED or RECOVERY_REQUIRED (old document still intact)
```

- Hold complete logical graph and channel controls in the session model throughout the device change. For unchanged hardware port identity, compatible remapping is eligible; for swapped/disappeared/ambiguous ASIO4ALL WDM pins, preserve the routing *intent*, but mark unresolved bindings DISCONNECTED / NEEDS_REMAP and stay muted.
- If rate/block changes, call correct JUCE/VST3 preparation on control-supported threads; vendor processing tails may reset and audible interruption is unavoidable. User values, FX order and available serialized plugin state must survive. No guarantee of seamless hot-switch between clocks.
- A rejected driver open, partial readback, post-open identity mismatch or plugin prepare failure may never publish a default mixer as the accepted document. An unavailable plugin becomes a visible offline/missing placeholder with its stored state retained; it cannot cause accidental bypass of every other chain.
- Publish new graph and document binding together only when identities and routes are valid, with bounded RT-safe handoff; report route acceptance separately from device openness. No per-callback locks, allocations, logging, disk I/O or vendor GUI work.
- After success the mixer should **LOOK THE SAME**. Physical device-bound badges alone may change to NEEDS REMAP. Explicit user ARM after checking a real input and real output is still required.

## 4. Persistence & recovery, on user's actual Studio workspace

Use the existing `PortableWorkspace::dataRoot()` and current Studio backup conventions. Do not create a competing config root or store project truth in browser localStorage.

**Versioned schema** `arsonmix.mixer-session.v1`, with build ID, schema version, stable document UUID, document revision, graph IDs, typed processing topology, logical routing, source intent, FX slot IDs/canonical catalog IDs, VST/plugin state SHA references, EQ parameters, snapshot metadata, UI view state and external hardware profile reference. Enforce total size, per-effect state caps, node/edge limits, unique IDs, acyclic graph and supplied plugin hash checks. Migrations must not coerce unsupported dynamic buses into 10+4 or silently discard WIRE.

**Store layout (proposed):**
```text
Studio/Sessions/<session-uuid>/
  HEAD                         # small atomic generation pointer
  versions/<generation>/
    session.json               # complete validated mixer document
    blobs/<sha256>.bin         # content-addressed vendor state
    manifest.json              # per-file SHA, schema, length, source revision
  recovery/                    # bounded last-known-good / failed draft metadata
Studio/DeviceProfiles/<profile-id>.json  # independent hardware preferences
Studio/Shows/                             # existing user-named Shows untouched
```

Write **stage -> validate -> readback hash -> atomic commit/pointer swap**. Preserve at least two last-known-good generations, never overwrite a valid HEAD with a partial write. Include bounded orphan cleanup only off RT and only after successful commits. Support crash between any two filesystem operations and corrupt or read-only folders; load last-good and give an actionable read-only/recovery status rather than resetting to defaults. Verify portable/installed data roots use identical semantics.

**Autosave policy:**
- Save only native **accepted** changes (not pending GUI gestures) and only when the accepted document revision advances. Debounce normal scalar changes, coalesce fader movement and persist a compact document generation; a single fader drag is one accepted gesture, not 60 writes per second. For structural edits, write a checkpoint at acceptance and avoid unrelated repeat snapshots.
- Vendor state can require different safe thread-affinity/quiescence: separate its checkpoint from high-rate fader saves. Never call arbitrary VST3 `getStateInformation` from realtime callback or while device teardown is in progress. Publish “Latest saved / Pending plugin state” truthfully.
- Manual **Save Snapshot** creates an immutable named revision; **Recall Snapshot** preflights and atomically prepares, with a one-step undo checkpoint. **Latest Session** is not a snapshot scene, and **Save Show** is not startup autosave. Preserve existing Shows/Studio Backup and introduce explicit user-level session import/export separately.
- On normal application exit, attempt a bounded final commit on the correct non-RT owner. After crash, prefer last verified fully committed generation; never trust a partial last gesture or partial plugin blob.

**Cold startup**:
1. Verify last-good session on disk (schema, checksums, stable IDs). Render saved mixer workspace even with hardware disconnected; show clearly which device/profile is missing.
2. Discover hardware without choosing an unsafe substitute. Match exact physical port roles only with sufficient identity evidence. Unresolved physical routing gets NEEDS REMAP, all outputs DISARMED and a non-destructive device selector.
3. Prepare graph after device has been explicitly verified; publish native acceptance/ACK, then offer user-controlled ARM. Do not start loud audio from persisted "MONITOR ON" or saved meter levels.

## 5. UI acceptance on the ONE approved /preview/

Keep current compact premium console; no new bulky wizard. A small existing status area displays e.g. `Latest session saved`, `Saving…`, `FX state pending`, `Output muted · verify mapping` or `3 inputs need remap`. A single **Snapshots** drop-down offers named Save, Recall, Rename/Remove and Restore Last Good. Device change does not trigger a full track list re-creation; reconcile by stable ChannelId and only update physical pin choices/status. Never render `None` over a saved but temporarily unresolved physical source *intent*. Never claim the old input is currently LIVE without driver readback.

## 6. Provenance: approved latest icon is a release contract

Brand reference = current approved `assets/icon/brand-source-128.png` and committed `assets/icon/arsonmix-app.ico`, `site/favicon.ico`, `site/favicon-16x16.png`, `site/favicon-32x32.png` on latest reconciled `main`. Current `CMakeLists.txt` uses `ICON_BIG/ICON_SMALL` from generated PNG; `src/Main.cpp` calls `setIcon(approvedAppIcon())` for windows; `build_pages.py` includes favicons in public preview. All are **source wiring**, not proof of installed EXE.
- Fix the exact packaging verifier interpreter mismatch without disabling the SHA test.
- Verify source hash + committed icon bytes, run consistent generation, assert EXE PE icon group rendered pixels match approved PNG at supported sizes; check native window/titlebar and WebView2 favicon after build. Validate actual artifact exe and ZIP SHA + embedded sourceCommit. Do not claim the icon is delivered while the ZIP lane is red.
- Prefer eventually shipping byte-exact pre-approved sealed icon resources across build machines (no Python/zlib-dependent compressed-byte drift); retain an independent derived-pixel verifier.

## 7. Deterministic qualification matrix

**Pure native session/control tests**:
- 10CH+4AUX and dynamic GROUP/FX RETURN/ChannelId/WIRE multiple edges: capture doc -> change device -> same IDs and exact fader/EQ/FX/bypass/latency/graph controls -> reconnect (including ASIO4ALL reorder).
- Device dialog open/cancel, same-device APPLY with unchanged pins, sample rate/buffer only, ASIO closed reopen, changed Realtek/VB-Cable pins, device busy/rejected, rollback success and failure. In ALL cases: original document stays complete; no auto monitor ARM.
- Disconnect/reconnect while current source identity absent, invalid sparse pin indices and ambiguous ASIO4ALL names; never silently wire channel A to device B.
- Vendor VST3 state success/failure/stale lease, editor open, missing plugin, plugin changed sample rate, save/recall mid-graph change. Exact acknowledged prior generation retained.
- Persistence fault injection: crash after stage/manifest/rename/HEAD, corrupt JSON/hash, low disk, read-only portable path, malformed graph, duplicate IDs, oversized FX blob, migration from ShowV2; no truncated/excessive memory or write in RT.

**UI/Windows/physical gates**:
- Full Chromium captures identical MIXER/FX/ROUTE visual positions at 1366/1680/1920 and bank mode. Device-change ACK must preserve every on-screen accepted control and stable wire ID while only hardware-unsupported port states become NEEDS REMAP.
- Exact-SHA Windows MSVC + native CTest + independently verified FAT ZIP. Pin/branding PE evidence and clean reinstall/portable second-run last-session recall.
- Physical WASAPI and ASIO4ALL with VB-Cable capture and Realtek render: compare same mix/FX order before/after reconfiguration and relaunch, no phantom output to speakers, measure crackle/latency/clock drift. Software PASS != hardware PASS.

## 8. Milestones (one meaningful integration PR, avoid naive patch stacking)

| Phase | Product deliverable | Exit criterion |
|---|---|---|
| P0 release integrity | Fix packaging Python provenance, source/PE branding checks | Same-SHA FAT ZIP produced, independently verified, not claimed physical pass |
| P1 state ownership | Document/DeviceBinding separation; no mixer destructive mutation when opening/changing Audio Setup | All supported mixer controls preserved across ASIO/WASAPI/same-device APPLY and driver failure; native regression matrix |
| P2 persistent work | Versioned full ChannelId SessionStore, fast accepted-revision autosave, named snapshot & crash recovery | Complete dynamic graph and available FX state round-trip after cold restart, corruption rollback; no RT cost |
| P3 hardware recovery UX | Identity-safe physical port profile/remap, missing VST placeholder and explicit re-arm | VB-Cable→Realtek ASIO4ALL and WASAPI test; no silent misrouting/reset; readable diagnostics |
| P4 release qualification | Whole workflow, UI parity, exact SHA ZIP and actual hardware FAT | Repeatable soak/clock/latency/crackle evidence; staged PR review then merge main when qualified |

**Conservative launch scope:** do not silently migrate legacy Show storage into complete SessionStore. Full session serializer must first round-trip native ChannelId state including extra buses and WIRE before making autosave the default. Do not use blind `startUnpatchedProductionSession()` as a recovery step once a user-authored document exists.

## Handoff and safety

- Current main at audit: `f96c09ec33da36c723aff3ae73b9b42d7db3fa7e`. Current draft integration PR #418 at audit: `9de9756c8303a081918bc9fd23195186e1e68128`. Both must be rechecked before implementation and merge.
- The architecture above is a **design**. Only the packaging interpreter fix is small implementation work in this checkpoint. No claim of implemented session retention, autosave, plugin state preservation across hardware restart, or physical ASIO4ALL PASS yet.
