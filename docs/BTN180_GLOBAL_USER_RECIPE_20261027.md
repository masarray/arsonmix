# Global BTN-180 hardware pad — owner recipe, 2026-10-27

Decision: user rejects synthetic toolbar crystal and Button 19. Reuse authentic BTN-180 for all 7 toolbar controls, 8 left-rail controls, header Audio Device, source utility and channel actions. Keep markup click/data attributes and span labels, but replace navigation `fx-nav` classes with `btn-180`. No JS or audio-backend changes.

Material: solid OFF `#b3b3b3`, border `--b180-edge`, dark ink. Use precisely the requested seven resting shadow layers, starting `inset 5px 5px 2px -1px rgba(0,0,0,.30)`; `:active` sinks 1px with `inset 3px 2px 2px -1px rgba(0,0,0,.50)` and retains seven components. ON changes color only, no permanent shrink/recess. The skin is shared via `20-button-180.css`; the obsolete Button 19/experimental crystal `21-` module is now a no-rule placeholder. `40-rail-pan.css` retains left-rail button geometry and PAN layout.

Semantic palettes: **utility polarity/HPF** soft cyan `#83d6e0`; **left rail** soft magenta `#db9cce`; **toolbar selected** silver-white `#f0f1f3`. MUTE coral, SOLO amber, MASTER stereo blue, mono red/black ink preserved. One-shot toolbar commands never latch.

Validation: all 15 keys have class `btn-180`; no `fx-nav` elements or Button 19 shadow; same seven physical layers in OFF/ON; `1px` transient press with unchanged width/height; page state, responsive 13/15/12 PAN, fixed LED meters, original PNG faders, WebView2/standalone CSS parity and native test suite must remain green. CI does not prove subjective visual approval or hardware audio quality.
