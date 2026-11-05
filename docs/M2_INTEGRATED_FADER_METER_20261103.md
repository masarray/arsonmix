# ArSonMix M2 — Integrated Fader & dBFS Meter (2026-11-03)

## Purpose
Replace the visual disconnection between two very thin LED bars, an elastic empty
fader column and a distant gain ruler with one compact hardware-inspired instrument.
This is the *reference Pages GUI only*, not native DSP parity or hardware validation.

## Geometry
- 51px existing meter housing (28px light well + 21px dBFS ruler), *independent*
  of gain ruler; each stereo LED grows from 6 to 8px, 2px inter-lane spacing.
- 32px centered fader pocket, 27×54px original Ari cap artwork, top/bottom 27px
  cap-center travel in exact alignment with the gain ruler and hidden input.
- 26px gain ruler, with 3px controlled grid gaps. Entire 51/32/26 assembly is
  optically centered rather than occupying one elastic middle column.
- A subtle static instrument plate and a channel-tinted under-cap illuminated
  slot. No animated shadows, blur, DOM nodes, repaint polling or JS audio work.
- The UI's finite -60 dB minimum is displayed as -60, not a fictitious -infinity
  gain cut. Actual audio behavior is unchanged.

## Invariants
- dBFS meter 0…-60 continues to reflect signal, *not* the gain position.
- Gain -60…+10 (or SEND +6) is independent, 0 dB is unity detent.
- Exact current PAN/FX/WIRE/icon surfaces, native-engine boundaries, meter
  preview math, and existing mouse/pointer/keyboard bindings remain unchanged.
- Real browser geometry assertions cover input and Master across density modes,
  viewport widths and published aliases. CI visual captures remain inspectable.
- Restore point is the merged main commit of GUI M1.2, `37dd696`.
