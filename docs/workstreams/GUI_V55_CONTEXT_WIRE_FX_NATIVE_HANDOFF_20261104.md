# V55 — WIRE socket paint, fader cap depth and native FX handoff

**Reference only; not native PCM.** Baseline `main@1e5a835f6af1125a451c8a9a77cb5556ce0a3152`. V51/V52 meter/fader geometry, V53 FX rack, V54 FX bypass state semantics remain authoritative.

## Implemented GUI
- `data-wire-paint=idle|connected` is projected from **visible accepted** `wireEdges`. This is a rendering state, not an audio route. `data-wire-connected` remains true for a committed but hidden/off-bank wire; only a visible physical wire illuminates its endpoint. No legacy SEND or selected-channel state can pretend a committed wire. Hover/drag/magnetic snap are explicitly transient.
- CH1/CH2 idle WIRE IN/OUT remains graphite; replug/Undo refresh the paint without additional per-frame rendering.
- Silver/rose PNG fader cap retains existing dimensions/travel; larger three-layer **static CSS** shadow, no filter, canvas or animation.
- V54 already implements header `fxOn` for whole-channel FX chain, and each `slotBypass[index]` for only its insert. Turning the header OFF must preserve all slot bypass flags and stable `fxInsertIds`. Never introduce duplicate bypass state owners.

## Engine/native GUI connection points
Existing authority: `contracts/console-puzzle-v8.json`, `AGENTS.md`, ENGINE #266, native GUI #271, FAT #270. Use schemaVersion=6 messages `arMixConsoleIntent` -> `arMixConsoleAck` -> `arMixConsoleSnapshot`. Required intent: `requestId`, `deviceEpoch`, `baseGraphRevision`, `baseRosterRevision`, `operation`, `targetChannelId`, `payload`. Only matching same-epoch/revision accepted snapshot paints LIVE; pending/error must not change accepted chain or wire.

| GUI control | Stable target | Proposed installed-operation requirement |
| --- | --- | --- |
| Channel FX header | ChannelId | Whole chain bypass; preserve per-insert bypass, editor, plugin lease and PDC |
| Individual FX power | ChannelId + stable insertId | Toggle **only** that insert, preserve sibling state and chain flag |
| FX drag grip | ChannelId + insertId + target index | Same-channel prepared reorder; reject copy of VST3 by name |
| WIRE node | ChannelId + edgeId | Only accepted `graph.edges[]` may show committed jack; focus/visibility is a projection |
| Fader | ChannelId | Existing bounded realtime gain operation, no graph rebuild |

**Capability gating:** propose names `SET_FX_CHAIN_BYPASS` / `SET_FX_INSERT_BYPASS` only for discussion with the native owners; do NOT advertise these as installed `supportedOps` or send unsupported intents. Native state must be prepared off callback and accepted atomically; callback never allocates, locks, touches UI or loads/unloads VST3. Reference GUI currently fails closed in native mode (`deferNativeFxIntent`) until native owners install the actual operation+ACK+accepted snapshot.

## Gates
Run `python tests/pages_v55_context_wire_fader_smoke.py`, previous V54 FX chain/slot+native fail-closed smoke, full Chromium exact-head CI and visual screenshot review. Preserve V51 meter SHA/geometry; no hardware FAT claim. Wire canonical MAIN implementation is separately owned by Packet A runbook.
