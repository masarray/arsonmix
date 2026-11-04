# ArSonMix approved PNG buttons — preview-only visual owner

This directory holds **only** the approved MUTE/SOLO/PFL pixel assets:
`assets/mute-solo-pfl-56x37.png`, `assets/mixer-control-keys.css`, and
`assets/mixer-control-keys.js`.

The published `/preview-buttons/` HTML and **all other dependencies** are
built from the current `site/preview-v2/` in `scripts/build_pages.py`. Do
not commit an independent HTML copy or frozen WIRE/fader/FX styles here.
Asset parity and ON/OFF geometry are verified by
`tests/pages_png_buttons_wire_reconciliation_smoke.py`.

`/preview/` and `/preview-v2/` retain their original renderer and
routing logic until separately approved. Preview is simulated; native GUI
`web/` and the audio engine are not modified by this variant.
