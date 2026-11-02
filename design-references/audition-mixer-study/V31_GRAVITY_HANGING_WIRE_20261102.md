# ArSonMix V31 — Top patch bar with gravity-hanging signal cables

Date: 2026-11-02. PR #278 isolated browser reference; **NOT** native audio.

## Visual and interaction contract

- OUT solid pin, recessed IN socket and WIRE gain knob stay in their current fixed 24/28/22px grid, now at the **TOP** of each expanded WIRE compartment.
- The remaining WIRE compartment below the controls is a neutral dark suspended-cable canvas. Remove duplicate painted words/labels from that open stage; channel identity remains in the existing header badges.
- Every connection is a downward-draping catenary-inspired cubic Bézier: OUT starts down, IN terminates up, left-to-right and right-to-left both behave identically.
- Sag scales sublinearly with horizontal distance and is clamped to actual available WIRE stage height. It must **never enter PAN/fader**. The previous upward 164px arch is eliminated.
- A compact damped spring runs for **at most 24 animation frames** after accepted new/rerouted cable, with no global timer, permanent animation loop or audio-thread effect. Respect OS reduced-motion. Drag preview, magnetic snap and unplug preserve the existing V23–V26 interactions.
- Collapsed WIRE remains 23px high; no connectors/cables outside it, only original DOT/socket summary. Focus, OUT only, All display options unchanged.
- Preserve V30 graphite keycaps, PAN, FX sizing, fader asset and graph/mult/Bus-only semantics.

## Research

Image-Line FL Studio Mixer Menu describes optional **Routing cables** with “Jiggly goodness”, while mixer guide describes directional track routes and selected-source sends. ArSonMix's curve and spring are a fresh interpretation rather than copied FL Studio source code.

## QA

- Full deterministic model smoke plus dedicated Chromium `visual_wire_v31_gravity_qa.py` across 1366×768, 1680×900, 1920×1080.
- Verify node rectangle above cable stage; Bézier control points and midpoint sag below endpoints; bottom depth bounded by WIRE; forward and reverse links; All and fold controls; zero browser errors.
- Legacy V15/V18/V19/V22/V26/V29/V30 workflows must remain green before promoting preview.
