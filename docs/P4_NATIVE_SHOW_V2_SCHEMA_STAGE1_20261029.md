# P4 stage 1 — Native ShowV2 / SnapshotV2 schema contract (2026-10-29)

## Why this is a separate PR
P1J in main now preserves actual running channel VST3 instances across safe route edits, but its accepted route/session is volatile. The left rail's localStorage snapshots are **UI-only** and cannot restore a real 10-channel performance. The first production-safe step is a strict native serialized data contract which cannot accidentally open a different sound device or touch the realtime callback.

## Delivered by this stage
- `src/NativeShowSchema.h`: two native JSON document kinds, `arsonmix.native.show` and `arsonmix.native.snapshot`, with explicit `schemaVersion=2`, stable UUIDs, bounded names and monotonic positive millisecond timestamps.
- Show metadata stores an ordered bounded list of snapshot UUIDs, current snapshot ID and exactly ten **stable indexed** channel recall-safe flags. Actual snapshots are separate documents, not unbounded nested arrays.
- Snapshot stores **exactly ten** source/physical-port mappings (disconnected/mono/stereo), linear fader and PAN, MUTE/SOLO and to-MASTER, PRE/POST sends (32 total max), ordered per-channel FX references (eight max each), catalog ID, VST UID, SHA256 blob digest and bypass, plus MASTER gain/PAN/MUTE/MONO.
- A plugin state *digest/reference* is not a vendor VST3 state capture. It must point to a verified immutable binary blob created by a later storage stage. This stage does not serialize Orbit automation, HPF, channel trim, full multi-FX bus layouts, external sample libraries or output device driver settings. Those capabilities must be versioned and implemented before a full-scene claim.
- `Codec::decode` rejects malformed, oversized, unsafe or out-of-range documents, duplicate UUIDs, invalid references and unsafe topology basics without changing an already accepted output object. Physical port indexes are data **only**: they are never automatically activated on another PC. Cycle detection and device/plugin compatibility remain mandatory native routing preflight before eventual recall.

## Runtime and testing boundary
- Serialization is control-only. The production mixer callback, FX graph, Orbit, GUI CSS, master meter, portable vault, dialog and WebView2 code are **unchanged**.
- `tests/native_show_schema_test.cpp` exercises round-trip JSON, ten real indexed source records, sparse stereo pins, PRE sends, FX digest references, channel safes, unknown schema, duplicate snapshot UUIDs, invalid names, invalid plugin/port/pan/send and failure atomicity. The existing canonical Windows MSVC + CTest workflow builds this new test; it does not spawn another CI pipeline.
- There is no save-to-disk/backup integration and **no native recall** in stage 1. Neither green CI nor a Windows artifact means physical audio was tested or that issue #15 is closed.

## Next PRs (separate gates)
1. **P4 stage 2**: portable Studio/Shows transactional storage + immutable blob checksum verification + recovery journal, budgeted file/entry limits, and verified backup inclusion. Capture actual accepted P1J route and real vendor getState on compliant control/affinity thread; identify unsupported capabilities, never fabricate them.
2. **P4 stage 3**: read-only native show/snapshot metadata via WebView2 ACK, migrate UI-only localStorage as explicitly *legacy UI* (no fake plugin states).
3. **P4 stage 4**: full prepare/validate/ACK-gated recall, device-epoch/port remap fence, per-channel safes/undo, controlled output mute/de-click and transaction rollback. Avoid disk, JSON and plugin vendor lifecycle in RT.
4. Windows 100/125/150/175% first-open Audio Setup DPI and actual ASIO/WASAPI #15 physical FAT remain independent release gates.

Do not merge historical PR #128 wholesale; keep the P0 #129 gray/green toolbar, lightweight pointer skin and approved 10CH+MASTER layout.
