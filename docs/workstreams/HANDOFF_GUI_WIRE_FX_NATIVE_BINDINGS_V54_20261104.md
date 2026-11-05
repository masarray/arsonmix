# ArSonMix V54 — GUI/Engine binding contract (2026-11-04)

Owners: GUI epic #351, ENGINE #266, native GUI #271, integration/FAT #270, physical #15. Read AGENTS.md, WIRE-first PRD/ARD and contracts/console-puzzle-v8.json before implementation. V54 Pages is SIMULATED only.

## Canonical socket semantics

- Physical OUT/IN dot connected attributes derive only from enabled accepted WIRE graph edges (Pages demo currently uses wireEdges). CH1/CH2 legacy SEND/OUT/split metadata, selection, hover or a pending cable cannot imply a WIRE plug. Distinct hover/drag/snap states are transient. A real off-bank edge remains connected while its SVG cable is not visible. Stable ChannelId and edgeId are mandatory; no CH1/CH2 index-based paint.
- Native graph edits must use requestId, deviceEpoch, baseGraphRevision, baseRosterRevision, targetChannelId, and explicit source/dest IDs. Commit only matching ACK and accepted snapshot, preserve last-known graph on rejected/timeout/stale. One candidate graph DAG validation and atomic publish to one callback; no JS PCM graph.

## FX chain versus FX insert

- Header controls per-channel chain-enabled state. Header OFF bypasses all serial inserts without modifying any per-insert bypass bit, parameter chunk, editor, latency or lease. Header ON restores their previous states. Effective slot processing is channelFxEnabled AND NOT slotBypassed.
- Slot power controls ONLY stable (ChannelId, insertId) bypass state, not index-only, not the FX chain or another channel. Slot toggle while header is OFF must persist and become effective only if header later ON. Reorder preserves IDs, parameters, bypass, PDC and editor.
- Native operations proposed for ENGINE owner review: SET_CHANNEL_FX_ENABLED(targetChannelId,enabled) and SET_FX_INSERT_BYPASS(targetChannelId,insertId,bypassed). Names are proposals, not an already installed ABI. Advertise through graph.capabilities.supportedOps ONLY after installed DSP and native ACK support. Existing schemaVersion=6 v8 envelope remains authoritative.
- FX removal/reorder/plugin editor and genuine VST3 load require separate native intents, accepted snapshot and prepared host lifecycle. V54 reference GUI refuses local FX bypass/remove mutation in detected native WebView/bridge environment until these handlers are installed. No false LIVE state.

## Physical fader and FAT

- Fader thumb elevation is static CSS on original PNG; no audio or native intent. Existing fader level remains bounded RT parameter, no new DSP callback, per-frame layout or GPU filter.
- ENGINE #266: test two independent genuine VST3, per-slot bypass, whole-chain bypass and restoration, stable insert leases, unchanged PDC, real PCM; WIRE connect/disconnect with fan-in/fan-out, no audio callback allocation/lock.
- Native GUI #271: read only actual producer-generated snapshot with stable IDs; use arMixConsoleSubscribe/Snapshot/Intent/Ack and matched revision; never infer sound from Pages demo. Test accept/reject/stale/pending, bank change, pinned MAIN and recall.
- FAT #270: verify exact joint SHA, one installed callback, real pin paths, 0FX and 2FX, Show recall, 30-minute acoustic stability; #15 remains open until physical crackle evidence. GUI screenshot and Chromium CI are not physical audio evidence.

## V54 acceptance

Chromium tests/pages_v54_wire_fx_context_smoke.py verifies CH1/CH2 idle dots, real WIRE connection and Undo, FX header/individual independence including two inserts and stable IDs, native no-ACK fail-closed, Comfort/Standard/Compact at 1366/1680/1920. No cards, text badges, new workflow tabs, JS audio, or looping render animation.