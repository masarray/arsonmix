# V32 — Local soft-wire settle + cross-bank smart patch

Scope: **PR #278 browser GUI reference only**, not native ASIO / DSP / PCM. Existing top-mounted OUT/IN connectors, stable patch-bay row, V30 Focus/OUT/All and realistic keycaps remain intact.

## User scenario
1. Select CH01 **OUT**. This explicitly pins the source until a new OUT is selected or Escape unpins it. Selecting another strip's ordinary fader or name cannot silently retarget an active patch.
2. Navigate BANK Next to CH11. The compact WIRE ledger says **FROM CH01 · Track 1 ↶** and **Click any IN on this bank**. The source chip returns to its bank without changing routing state.
3. Existing edges (including active legacy SEND slots) appear as cables entering from a clearly labelled **CH01 · OTHER BANK** entry at the visible patch-bay boundary. Never draw a fictitious line to a hidden channel's arbitrary onscreen x-coordinate.
4. Click an IN on a remote bank: the same validated graph is used; duplicate SEND and feedback cycles remain blocked. Bus destination still requires VIA BUS ONLY / PARALLEL decision. CH01→CH11 is already enabled in the demo's legacy SEND slot; the IN is explicitly linked and duplicate insertion explains **ALREADY CONNECTED**. CH03→CH11 demonstrates a new parallel route.
5. Returning to source bank preserves links and selection. The full routing remains independent of bank display.

## Animation and performance
- The V31 global `wireSpring` made all visible edges wobble simultaneously. V32 gives an active key `sourceId-destId` to only the newly committed edge.
- Maximum motion 2.8 SVG units, exponential decay with one gentle settling phase, at most 18 `requestAnimationFrame` callbacks; accepts `prefers-reduced-motion`.
- Cache measured edge endpoint coordinates per layout render; each animation frame calls `setAttribute('d', ...)` only for SVG paths belonging to that edge (stroke and shadow). Other cable DOM, all mixer strips, racks, faders, and audio data never rebuild on the animation frame.
- A new accepted route invalidates the previous motion token. Collapsed WIRE clears paths; hidden-bank edges have no artificial canvas.
- Long cables are bounded by actual patch-bay compartment clearance; PAN remains untouched.

## Acceptance
Run `node design-references/audition-mixer-study/smoke.test.cjs` and `python design-references/audition-mixer-study/visual_wire_v32_cross_bank_qa.py` as well as the V15–V31 Chromium suite. Test full bank changes and linked legacy SEND, proper source persistence, independent wire path mutation counts, genuine Bus route choice, and return-to-source. Publish only after all checks succeed.
