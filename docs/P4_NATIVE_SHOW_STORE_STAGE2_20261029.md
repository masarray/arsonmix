# P4 stage 2 — Native Studio/Shows transactional generation store

**Status:** Engine-independent storage with versioned 10-INPUT/4-AUX snapshots; not yet wired to a live SAVE/RECALL button.

## Source and boundaries
- `src/NativeShowStore.h` owns CONTROL/IO-thread `commit(show,snapshots,vendorBlobs)` and `load(showId)`. The application must call it with its normal `PortableWorkspace::dataRoot()` (already `Studio/` in portable mode). All files land under `Studio/Shows/<show-uuid>/`.
- Each save checks ShowV2/SnapshotV2 bounded schema and ordering, exactly ten stable strips, FX catalog IDs and SHA256 digest references. Every referenced vendor blob must be present and content-verified; no fabricated VST3 state.
- Save stages `show.json`, ordered `snapshots/<uuid>.json`, immutable `blobs/<sha256>.bin` in a separate directory, verifies the whole generation, renames into `versions/<generation-uuid>`, then commits a **single small HEAD** via JUCE TemporaryFile flush/replace. Failure before/at HEAD retains the previous HEAD; abandoned generations are not live.
- Load verifies the HEAD UUID, bounded metadata, every snapshot and every distinct referenced binary hash, then publishes outputs to the caller together. Corrupt/missing data leaves prior caller objects intact. Distinct blob hashes are verified once; total reader I/O and accepted commit size are capped.
- Policy caps per generation: 64 snapshots, 128 supplied blobs, 8 MiB per VST3 binary, 128 MiB of assembled data. This is deliberately narrower than the standalone schema limit of 256 snapshots. Future scalable manifests need paging before raising these limits.
- `StudioBackup` already enumerates all ordinary files in Studio, so each Show HEAD/generation is included in the same independent SHA256-verified USB backup. `tests/studio_backup_test.cpp` now asserts it. Browser cache and diagnostics remain excluded.

## P1K AUX return storage extension (snapshot schema version 3)
- Ten INPUT source strips stay distinct from exactly four additional AUX stereo FX return lanes, identified `aux-001` through `aux-004`; each persists enabled, MAIN flag, mute, linear gain, and ordered vendor FX references.
- Up to 32 input-only `auxSends` persist source CH0..9, destination AUX0..3, PRE/POST, gain 0..1 and pan -1..1. The decoder rejects duplicate (source,destination,tap), disabled destination, invalid gain/pan and malformed/partial AUX arrays. No AUX feedback path is expressible.
- Newly encoded snapshots are **schemaVersion 3**; Show metadata stays V2. The `SnapshotV2` C++ DTO remains source-compatible but now serializes version 3. Older version 2 snapshots lacking AUX fields decode with all returns inactive. Version 2 payloads carrying AUX data are *rejected*: older executables must not silently drop AUX on re-save.
- INPUT and AUX VST3 state references share SHA256, UID, catalog identity and bypass validation. `NativeShowStore` refuses missing AUX bytes *before disk writes* and verifies every distinct referenced blob *before replacing loaded outputs*. Tests exercise corrupt FX, route rejection, legacy migration, rollback and portable backup.
- The extension is **not** live audio state capture/recall. Plugin state capture thread, device preflight and application of a prepared graph remain separate gates.

## Failure and recovery notes
- Retired generations are kept for recovery; lifecycle pruning and disk quota UI still require a later implementation. An unsuccessful HEAD replacement may leave an orphan version which is not automatically loaded.
- JUCE stream flush plus same-directory temporary replacement is not a guarantee against every sudden power-loss sequence; verified journaling/OS fsync policies and Windows fault injection remain future production gates.
- Physical ports are **never activated** during load. No VST3 instance is instantiated. `BinaryState` must come from proper VST3 state capture on its permitted thread. Stored references alone do not prove that capture occurred.
- No live GUI Show list, native vendor capture, Orbit state restoration, native mixer recall, safes masking or device-epoch ACK is delivered by this stage. The legacy localStorage sidebar remains UI-only. #15 zero-plugin hardware crackle remains open.

## Automated acceptance
- `tests/native_show_store_test.cpp` covers a real ten-strip snapshot with binary VST3 test bytes, immutable generation reuse, duplicate/missing snapshot rejection, bad binary hash, corruption rejection without modifying accepted outputs, and unsafe HEAD traversal refusal.
- This test is built and run with existing Windows MSVC+CTest pipeline; no parallel matrix or new publisher. Portable backup test additionally asserts native Shows are carried into the verified backup.
- Before merging: ensure exact-head native CI tests, usable artifact packaging, unchanged UI locks, and no RT callback changes.

## Next native gates
1. P4 stage 3: capture the accepted P1J session and actual VST3 vendor state through correct control/main-thread affinity, with explicit unsupported-field masks (Orbit/HPF/Master FX etc.). Publish metadata read-only through the real native bridge.
2. P4 stage 4: preflight/re-map devices safely, stage new immutable graph with plugin states off callback, structural MUTE policy, ACK-gated activation, rollback and native safes/undo. Never silently apply physical ports on another PC.
3. Real Win11 ASIO/WASAPI hardware FAT, first-open WebView2 and Audio Setup at 100/125/150/175% DPI; physical output/GUI behavior matters more than CI-only synthetic tests.
