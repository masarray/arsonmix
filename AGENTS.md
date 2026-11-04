# ArSonMix — ONE GUI, ONE NATIVE AUDIO ENGINE
**Binding owner decision — 2026-11-04. This page supersedes every conflicting historical GUI instruction.**

## The ONLY approved product GUI
**https://masarray.github.io/arsonmix/preview-v2/** is the finished GUI/UX. Source-of-truth: latest reviewed `main/site/preview-v2/index.html` and `site/preview-v2/assets/**`. The GUI is already designed. **STITCH THE ENGINE TO THIS GUI. Never recreate, imitate, redesign, simplify or make a competing native GUI to fit an easier backend.** Keep its exact material, geometry, bank/density/collapse, fader, meters, FX, PAN, buttons, selected-only WIRE and pinned MAIN behaviors. The reference can advance through reviewed main GUI changes; native integration must follow the current approved SHA.

`/preview/` is a separate PNG-button visual variant and is not the authoritative native target. Older PR #278/#285 mock studies, stale M3/V48 ports, `web/index.html`, old `web/mixer-layout.js`/M4 workbench, and ENGINE's `web/console-native.html/.css/.js` **are not product UI authorities**. Extract only validated native bridge/PCM/FX logic from them; do not ship their renderer. No hand-copied stale CSS/HTML duplicate.

## Active canonical documents
- [PRD — WIRE-first release](docs/PRD_WIRE_FIRST_LIVE_ROUTING_20261104.md)
- [ARD — native routing architecture](docs/ARD_WIRE_FIRST_ZERO_ADDED_LATENCY_20261104.md)
- [ONE execution handoff](docs/workstreams/HANDOFF_APPROVED_PREVIEW_V2_NATIVE_20261104.md)
- [Native socket schema v8](contracts/console-puzzle-v8.json)
- [Native GUI-stitch P0 issue #389](https://github.com/masarray/arsonmix/issues/389)

## Single product ownership (no duplicate streams)
| Track | Owner | Requirement |
|---|---|---|
| GUI visual/UX | latest merged `main/site/preview-v2/**`, GUI issue #351 | Preserve source and approved interactions |
| Native UI stitch | issue #389, one controlled integration branch | Deterministically embed same GUI into JUCE WebView2 and add thin native adapter |
| Native ENGINE | PR #266 and related native WIRE #387 | One installed ChannelId/EdgeId PCM graph and VST3 lease owner; **no alternative GUI** |
| Integration and release FAT | PR #270, native issue #192, crackle #15 | One joined SHA, Windows EXE, UI/PCM/hardware evidence |

Legacy native GUI PR #271 and stale M3 integration PR #356 are superseded UI implementation directions; close their competing work, retaining only selective functioning native adapter code. Do not close or discard actual audio engine progress.

## Architecture invariants
- Exactly ONE JUCE primary realtime audio callback, ONE native accepted graph, ONE native VST3 host/lease, ONE GUI projection; no duplicate WebAudio/JS audio engine, callback, router or plugin host.
- A deterministic generator must bundle `site/preview-v2/**` into Windows WebView2 resources, with source+asset SHA256 manifest and full dependency mapping in CMake/BinaryData/ResourceProvider. No alternate entrypoint in compiled EXE.
- Existing v8 `schemaVersion=6`: `arMixConsoleSubscribe`, `arMixConsoleSnapshot`, `arMixConsoleIntent`, `arMixConsoleAck`. Exact canonical decimal-string uint64 ChannelId/EdgeId, device epoch, graph/roster revisions; intent→matching ACK→later accepted same-revision snapshot. Pending/rejected/stale/unsupported != LIVE.
- WIRE, OUT, IN, MAIN, Matrix, Navigator, Scenes, Undo, FX and metering use native accepted state. Native physical ports are verified active pins. Browser routing demo IDs are **never** native IDs or PCM evidence.
- Preserve default real CH→MAIN edge, selected-only visible WIRE, matched MAIN fragments at inner jack dot only when selected and expanded, no collapsed cable noise, OUT lit only for **non-MAIN custom outgoing edges**. Reject cyclic/duplicate/unsafe/stale edits and preserve last accepted sound.
- Prepare structural changes off audio callback, publish immutable accepted graphs and maintain real VST3 lease/editor/PDC safely. Audio callback = zero allocation, locks, UI, disk, VST scan, driver discovery or JS.
- Preserve existing working CH1 vendor VST3, 10CH+4AUX, Show and device routes during universal ChannelId migration. Do not fabricate CH11, fx return, monitor or acoustic clean status.

## Fast release execution
**M1** compile real `/preview-v2/` HTML/CSS/assets into Windows EXE and screenshot-match at 1366×768, 1680×900, 1920×1080 across bank/density/collapse. **M2** bind native accepted intent/snapshot/ACK to unchanged controls and correct rollback. **M3** connect WIRE, real PCM, VST3, faders/meters/hardware to sole graph. **M4** complete single combined exact-SHA Windows CTest/Playwright/FAT incl. two real VST3, PCM impulse, ASIO/WASAPI and dry crackle #15. Run `node scripts/check_console_puzzle.mjs --release` plus approved GUI provenance check. No customer UAT until coherent product evidence; physical FAT absent = PENDING.

**Definition of DONE:** one Windows application that looks and behaves like `/preview-v2/` and truly runs native low-latency mixer PCM/VST3 with accepted readback. Green Pages tests or independent engine CI do not equal a finished product. Older v5/v6/v7 and PR #278 documents are historical **for GUI choice only**; realtime safety/ABI/device truth still apply.
