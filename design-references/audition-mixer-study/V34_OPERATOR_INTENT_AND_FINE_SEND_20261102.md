# V34 — Operator-Intent WIRE and Fine SEND Gesture

Date: 2026-11-02. Scope: PR #278 isolated GUI reference, **not** JUCE native routing, PCM, ASIO, VST3, or a production release.

## Decision grounded in professional console workflow

FL Studio's official mixer reference separates the **source → destination send switch → per-link gain** from external I/O. For a live operator mixer, ArSonMix goes one step safer: **ordinary channel focus never arms or retargets a patch**. Only the WIRE OUT node can deliberately pin a source; receiving IN sockets, universal destination picker and MAIN toggle require that pin. Fender Studio Pro-style adaptive console layout remains intact; no DAW timeline, new routing matrix or second per-strip SEND panel.

Official reference: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm

## User tasks and contract

| User task | Expected operation | Never allowed |
| --- | --- | --- |
| Mix normally | Click channel, FX, fader, PAN, MUTE/SOLO; WIRE stays dark when idle | Silent update of patch source or graph |
| Open patch | Expand WIRE, explicitly click OUT on a source | Selecting a channel alone arming OUT |
| Connect | Choose receiver IN or drag from OUT; accept cycle/duplicate/BUS role checks | Clicking idle IN to modify any graph |
| Multiple channels/banks | Keep explicit source pinned while moving bank, show named FROM and any-bank destination picker | Invent visual cable to invisible channel positions |
| Inspect | Header and focused cables only after source armed | Dense colorful wires on normal mixer selection |
| Send gain | Compact tactile knob, drag ~3 px/dB or Shift ~14 px/dB, step 0.1 dB, wheel 0.5 dB, double click unity | Short 22px travel, changing a different link |
| Cancel | Pointercancel restores original knob gain; Escape disarms WIRE; no accidental connection | Mutating graph on cancelled routing gesture |

## Implementation guard rails

- `selected channel` (mixer inspection) is not `wireSourceId` (active patch origin); source is considered **armed** only when both `wireIntent` and `wireSourcePinned` are true.
- `wireTray`, focused legend, rendered paths, receiving IN, MAIN toggle, and universal bank picker use the same intent gate.
- Existing `wireEdges`, `sendSlots`, primary OUT and split projection remain compatible; the browser still has multiple read-only routing projections, **not yet** a unified accepted native signal graph.
- Legacy send cannot silently create a duplicate wire. BUS routing requires explicit Route Only or Parallel choice.
- Background paths remain restricted to the WIRE compartment. V32 per-edge geometry cache/settle, gravity-hanging arc and reduced-motion gate are retained.
- 22×24 knob footprint does not change strip geometry; 30×64 metal fader thumb and pinned MAIN stay.
- No UI animation or pointer work in audio callback: this isolated reference has no PCM.
- No claim that a browser mock or fake LED meter proves native sound. Real integration requires accepted graph ACK/revision/epoch and Windows physical FAT.

## Evidence and release gate

- `smoke.test.cjs` verifies that idle IN, ordinary channel selection, existing route projection and explicit source actions cannot accidentally patch.
- `visual_wire_v34_intent_qa.py` drives real Chromium pointer gestures for idle/armed state, 0.1 dB Shift fine adjustment, cancel rollback, pinned cross-bank source, Escape and view-only switch at 1366×768, 1680×920, 1910×912 and 1920×1080.
- The established V15/V18/V19/V22/V26/V29/V30/V31/V32 Chromium suites must also pass on one pinned commit.
- PR #278 stays **Draft** until exact-SHA CI plus owner physical visual review. Public preview should not be promoted until V34 acceptance.
- Next: native V35 requires stable typed edge ID graph (SERIAL GROUP, PARALLEL SEND, SPLIT, LEGACY projection, hardware port), snapshot/SoF authority, accepted ACK and no false live status.

Do not merge or publish the reference as a native audio feature.
