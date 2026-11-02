# ArSonMix V18 — Pull Cable From Receiving IN, Replug or Unplug

**Status:** implemented on PR #278 isolated GUI reference; V18 does **not** perform native ASIO/PCM audio rerouting.

## Fast soundman operation

- **Grab:** move pointer to a connected **IN socket** (or its near-side surrounding tray, within 22 CSS px of its center), press and drag.
- **Move:** the connected cable's old segment disappears, its round tip follows the pointer, and the original socket shows a recessed empty hole. The original connection is still preserved in the model while dragging.
- **Replug:** bring cable within the target's 42px magnetic halo, or into another channel's WIRE compartment; the loose cable jumps visually to the destination socket. Release to transfer the **same** edge ID to that destination.
- **Unplug:** move beyond **31px** of the original receiving socket and release outside the magnetic area of any *other* receiving socket. This removes that one edge.
- **Accidental move protection:** a motion under the drag threshold remains a click; after starting a drag, returning to the original receiving socket preserves the connection. A canceled pointer gesture changes no routing.
- **Original single-click and OUT→IN drag** remain available; the dedicated × button and per-wire gain knob are retained for accessibility and keyboard alternatives.

## Audio-graph safety contract

1. Replug preserves ID, enabled state, level in dB, processing tap/mode and send-vs-primary semantics; it does **not** create another Send.
2. Reject a new destination on feedback cycles, already-linked destinations, legacy active Send duplication, or invalid receiving channel. On rejection **keep the previous connection**.
3. Primary **VIA BUS ONLY** can be moved only to another Bus or explicitly to MAIN; moving it to MAIN removes the original primary Bus edge and sets source primary output to MAIN. Unplugging a Bus-only wire leaves **SOURCE ONLY (No Main)**, never silently enabling an extra dry Master signal.
4. Main output hardware patch, device epoch and accepted native audio-graph revision still require production ENGINE work; the visual demo does not claim real PCM.

## Visual/performance contract

- Fixed four-column socket/knob/close geometry from V17; no resize/reflow when moving or unlinking an edge.
- One SVG cubic Bézier cable preview and a small unblurred circular plug at the free endpoint; target snaps to the receiving jack.
- No blur/filter-based animation; pointer redraw via requestAnimationFrame and model update on accepted release only.
- Source selection stays unchanged while dragging, and legacy click is suppressed once after an actual drag to avoid accidental toggling.

## QA evidence

- 221/221 model/DOM assertions PASS: move left/right, preserve gain/id, original snap-back, pull-to-disconnect, pointercancel, reject duplicate.
- Real Chromium connected-IN pointer tests at **1366×768**, **1680×900**, **1920×1080** PASS in [GitHub Actions #41393926429](https://github.com/masarray/arsonmix/actions/runs/41393926429).
- Preview publishing commit [`ba8c43e`](https://github.com/masarray/arsonmix/commit/ba8c43e68020b7bc1bd8c18e2f0edc9eb350f99c) is an atomic snapshot of the qualified PR278 source; GitHub Pages production release gate executes separately.

## Remaining native integration

Do **not** merge the HTML demo as a real DSP graph. Replace demo state with a validated `GraphIntent(oldEdgeId,newDestination | disconnect)` against a stable revision, reject duplicate/cycle/invalid device-epoch actions and update the GUI only after the engine's accepted graph ACK. Preserve audio-thread constraints, recovery and undo in native Windows qualification.
