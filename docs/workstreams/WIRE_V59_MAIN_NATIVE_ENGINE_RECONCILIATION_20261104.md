> **2026-11-04 SOURCE LOCK:** This file's V59 WIRE semantics remain valid, but all GUI implementation instructions involving old PR #271's independent renderer are superseded. ONLY latest `main/site/preview-v2/**` (https://masarray.github.io/arsonmix/preview-v2/) is allowed as the Windows GUI. GUI-native stitch #389, ENGINE #266/#387, FAT #270. [Current handoff](HANDOFF_APPROVED_PREVIEW_V2_NATIVE_20261104.md). Historical source SHA references cannot override latest approved GUI.

# WIRE GUI V59 → Native ENGINE handoff — reconciled main

**2026-11-04 · owner synchronization / integration contract · NOT native-audio release proof**

## 1. Exact state and single owners

- **Reference GUI source of truth:** `main@f182e676bdfb520a4f9fc521fa4748f3046df262` from merged [#383](https://github.com/masarray/arsonmix/pull/383). [#376](https://github.com/masarray/arsonmix/pull/376) installed the reference Guardian, accepted graph and paired MAIN fragments; V59 adjusted focus, collapse and OUT. GitHub Pages [published this SHA](https://github.com/masarray/arsonmix/actions/runs/41811077047). Reference code: `site/preview-v2/index.html` and `assets/mixer-wire-dots.css`.
- **Native ENGINE sole owner:** draft [#266](https://github.com/masarray/arsonmix/pull/266), `milestone/m5-universal-channelid-vst3-lease-migration-20261101`; B1 [#375] and B2 [#379] were absorbed into ENGINE ownership. C1 [#384](https://github.com/masarray/arsonmix/pull/384) is stacked on #266, **not yet merged to main**.
- **Production WebView/GUI owner:** draft [#271](https://github.com/masarray/arsonmix/pull/271), `work/operator-console-v6`, particularly `web/mixer-layout.js`. This is **not** the same file as `site/preview-v2/index.html`.
- **Joined FAT/release owner:** draft [#270](https://github.com/masarray/arsonmix/pull/270); hardware crackle [#15](https://github.com/masarray/arsonmix/issues/15) remains open. Do not promote universal/CH11/physical silence without evidence.

**Main is the visual/UX baseline, NOT a native engine cutover.** Reconcile #266/#271/#270 at a single qualified integration SHA. Engine #266 currently diverges from main; do not fast-forward or bulk-merge a 100+ commit native branch into approved GUI source. Retain the already merged `#383` UI and existing fader/meter/FX checkpoints. Superseded draft GUI [#377](https://github.com/masarray/arsonmix/pull/377) and [#381](https://github.com/masarray/arsonmix/pull/381) must not overwrite V59 with their stale full-tree changes.

## 2. UI behavior that the native accepted graph must drive

| User-visible invariant (V59 approved) | Native accepted-graph projection |
|---|---|
| `CH → MAIN` exists by default and remains a real accepted graph edge | One enabled `graph.edges[]` per channel→MAIN; never synthesize/duplicate PCM from a CSS fragment |
| Wire paths appear only for **exactly one selected channel** | Local selection filters accepted edges with `from === selectedId` OR `to === selectedId`; unrelated paths hidden; no `ALL wires` mode |
| MAIN source+receiver short curved fragments are visible only for uniquely selected CH and only when expanded | Both physical fragments project **the same** accepted `edgeId`; MAIN IN docks at inner dot; no full-cross-mixer MAIN path |
| Collapsed/folded WIRE is quiet | No visible MAIN fragment, tail/pseudo-cable or animated wire; flat low-key IN/OUT contacts only |
| OUT lights only for a real **non-MAIN** outgoing edge | `connectedOut = edges.some(e => e.enabled && e.from===id && e.to!==mainChannelId)`; default MAIN alone MUST NOT light OUT, expanded **or** collapsed |
| IN marks real accepted incoming (non-MAIN for channel strips) | Never infer connection from an unused legacy SEND, `track.output`, or CSS-only `data-wire-main-edge` |
| Selected cable, unplug/retarget/Undo retain exact edge identity | Use accepted decimal-string `edgeId` (not JS `Number`, channel index or color). Non-selected routes remain valid but invisible |
| No unique focus | No active physical MAIN plug selection and no cross-strip wire painting; Navigator/Matrix can inspect graph |

Source reference uses local numeric demo track IDs and `wireEdges` for browser simulation. **Never pass those IDs into native as ChannelId/EdgeId**, nor treat simulation's live graph as PCM evidence. Map the **behavior**, not its JavaScript object.

## 3. One v8 socket, one accepted native source

Use existing `contracts/console-puzzle-v8.json`, `schemaVersion=6` and events `arMixConsoleSubscribe`, `arMixConsoleSnapshot`, `arMixConsoleIntent`, `arMixConsoleAck`. A native WIRE intent uses `operation:"APPLY_WIRE_TRANSACTION"` and *the existing* `requestId`, `deviceEpoch`, `baseGraphRevision`, `baseRosterRevision`, `targetChannelId`, `payload:{reason,changes}`. The **only** target graph is the installed `LiveAudioEngine → LiveDryRoutingLab → DynamicNativeSessionPublisher` graph, not a browser route or second callback. All uint64 IDs/revisions are canonical decimal strings; use JS `BigInt` for comparisons.

Contract alignment required at **native producer #384 and GUI consumer #271** before claiming joined integration:

1. C1 `nativeBusStatus().channels[]` currently emits `format`, `primaryOutput`, `parallelSends` and sparse physical `source`; native `edges[]` emits `edgeId/from/to/tap/gainLinear/primaryOutput`. These are diagnostic/native-internal shapes, **not yet the v8 universal GUI shape**.
2. Production `web/mixer-layout.js::validSnapshot` currently expects each channel to have `processingFormat`, `inserts[]`, `sends[]`, a real `mainChannelId`, and each accepted AUDIO edge to have `enabled:boolean`, `signalKind:"AUDIO"`. Normalize from **real accepted native values** on the producer; do not fabricate physical pins, vendor parity or LIVE meter signals. The MAIN ID must come from a unique accepted native MAIN roster entry. `source.physicalPins[]` must come from the **verified** callback/device map if present.
3. C1 deliberately keeps `productionCallbackLive=false` pending full vendor FX/Show/monitor parity, but GUI `supported()` currently requires it to be true, while `validSnapshot()` rejects all nonempty graph rows/ops when it is false. This is a **real handshake blocker**, not fixed by green isolated native CTest. Agree explicitly on an interim, strictly gated **native WIRE accepted readback / NOT PRODUCT LIVE** state: actual installed PCM + native published graph + operation capability may be inspectable in joined FAT, while the product stays labeled NOT LIVE. No sample simulation or optimistic cable recolor. If producer cannot prove installed PCM, do not export active WIRE ops/channels.
4. `graph.capabilities.supportedOps` may expose `APPLY_WIRE_TRANSACTION` **only** when installed native routing and matching revisions are verifiably admitted. Unsupported legacy M4 must remain in its scoped `legacyM4` namespace. No synthetic `productionCallbackLive=true` to bypass the GUI's validator.
5. GUI only paints a new accepted cable after matching ACK `requestId/epoch/graphRevision/rosterRevision` **and subsequent native snapshot**. Rejection, stale request, device swap or pending keeps previous accepted graph. MAIN gesture removes only the selected edge.

## 4. Integration and release gates

- **Packet A (merged GUI reference):** #376 + #383; source, 1366/1680/1920 × density/drag/Undo/Scene/safety tests on main are qualified as **simulation** only.
- **Packet B (native owner):** #266 + #379, C1 #384 stacked; qualify PCM sample-exact direct and multi-hop fan-in/out, stable EdgeId, transaction rollback, one callback, real VST3 and no RT allocations/locks on exact Windows CTest SHA. **Do not force-merge #266 now**.
- **Packet C (native native GUI #389):** consume the normalized v8 native producer snapshot and same-revision ACK; selected-only visible cables and MAIN custom OUT logic must use accepted `graph.edges[]`; no browser route mutation as authority. Add a **native-produced** successful WIRE snapshot/ACK + stale/REJECTED replay test; never handcraft a purported native fixture.
- **Packet D (#270):** build one exact joined Windows SHA, test original M4 10CH+4AUX/CH1 editor/VST leases/Show and new WIRE, verify hardware #15. Only then consider `productionCallbackLive=true` and product-ready status.

No new CI workflow or duplicated audio engine is justified by this handoff. Use the existing fast contract test + owner-specific Windows/Chromium jobs, then one joined FAT gate. The next engine/GUI agent should start from **this exact current-main handoff**, not reopen stale GUI draft PRs.
