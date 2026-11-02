# V17 — Magnetic Physical Wire Patch Bay (GUI reference)

Date: 2026-11-02. Authority: isolated GUI PR #278. Status: implemented in HTML demo; **NOT native audio routing**.

## User-approved design

- Four stable controls in a physically fixed horizontal matrix: **OUT** (small filled metal jack), **IN** (larger recessed socket), **GAIN** (dark idle / destination channel accent when connected), and **REMOVE** (reserved slot hidden until linked).
- OUT and IN **do not shift** when routes become linked or unlinked; physical positions are not calculated from number of connections.
- A live IN shows a colored small center contact inside the larger recessed ring. Idle IN remains hollow.
- Rounded cables use symmetric cubic Bézier splines. Routing from the right to the left has exactly the same capability as left to right.
- **Preview** is solid, translucent and softly separated from the chassis. Only committed wires use a strong line; no dashed wire.
- **Auto snap** when the pointer is within 42 CSS pixels of a candidate socket *or* enters another strip's WIRE compartment. Snapping can cross the previously selected source and can target pinned MAIN.
- A drag released over PAN, MUTE, FADER or unrelated FX does not create a route. Connection is committed only on pointerup to a legal target.
- Hover and release candidate highlighting is only a visual affordance; graph safety/Bus choice remain existing V15 logic.
- Starting/canceling drag must not select a new source. An accepted drop selects its source.
- Pointer rendering uses a single requestAnimationFrame gate per frame rather than rebuilding the full mixer on every mousemove.
- WIRE collapse continues to hide wire graphics without deleting routing state.

## What was changed

1. `index.html`: stable four-column tray (24/28/22/14 px), idle pot placeholder, idle close slot; source pin visual smaller than destination ring. Active gain uses `--ch`, the strip's existing channel color.
2. `index.html`: `wirePathData` applies a bounded cubic spline with distance-dependent lift, symmetric for either sign of horizontal travel. `renderWireRouting` renders an opaque committed cable and a translucent solid preview, skipping off-bank edges without mutating them.
3. `index.html`: `wireSnapTarget` resolves the closest legal socket in a magnetic halo or WIRE compartment; `setWireSnapTarget` highlights candidate; `queueWireGestureFrame` throttles redraw. Pointerup recomputes snap to avoid using stale pointer frames. Cancellation clears only transient drag state.
4. `smoke.test.cjs`: fixed geometry and inactive/active controls checks; verifies no dashed wire, snap/drop integration, and source selection safety.
5. `visual_wire_v15_qa.py`: extends actual Chromium acceptance with an **off-center magnetic drop**, socket-vs-pin size, static knob coordinates before/after connection, and solid cable preview.

## Acceptance and evidence

| Check | Result |
|---|---|
| JavaScript inline syntax compilation (non-executing V8 parse) | PASS |
| Smoke test source syntax compilation | PASS |
| CSS brace balance | PASS |
| Static presence of snap/drop, stable grid, rAF gate and dash-free SVG renderer | PASS |
| Full smoke.test.cjs execution | NOT RUN in this session |
| Chromium / Firefox browser actual pointer geometry and screenshot | NOT RUN in this session |
| 1366/1680/1920 px visual QA and reduced-motion review | OPEN |
| Native ASIO/PCM graph, VST3 and accepted graph ACK | OUT OF SCOPE / OPEN |
| Pages preview publication | OPEN: reference branch is not automatically deployed |

## Manual operator check

Expand WIRE in the left section rail; select CH08 OUT, drag **left** toward CH03 and release **near** its IN rather than directly on the center. The wire should bend naturally toward the receiving socket, turn solid only after the drop, and preserve all IN/OUT/GAIN positions. Turn the CH03 gain knob; it should carry CH03's channel hue. Drag and release on a fader: it must not create a new edge. For a Bus target, continue to choose **VIA BUS ONLY** or **PARALLEL SEND** explicitly; this interaction does not turn the browser demo into native audio.

## Production handoff

The physical connectors are the operator-facing graph editor only. Native ownership must preflight source/destination/feedback, send-tap semantics, duplicate legacy SEND routes, device epoch and channel identity, and return an accepted graph revision before UI may label the wire as live PCM. Preserve one authoritative native signal graph and fail closed on rejected or pending transactions.
