# ArSonMix V14.1 — Smart Collapse and Premium Flat Metal

Owner acceptance (2026-11-02): For 7 of 8 upper-panel states, PAN, M/S/PFL, FADER and footer **do not change height or position**. The exception is `collapsed-route collapsed-fx collapsed-send`: the upper panels are all closed, so the PAN/SOLO move upward and the fader automatically stretches upward using CSS flex, not JS layout measurement. The footer stays docked to the bottom and the channel/MAIN rail follows.

SEND collapsed means **WIRE bay really collapsed**: 23px disclosure row, wire-deck and destination sockets hidden, and SVG graphical cable overlay hidden and not redrawn. No routing model/edge/gain mutation; expanding SEND immediately makes the same cable graph visible again. Controls remain available by expanding the SEND rail.

Visual material: dark satin graphite plate, solid face with only edge/seam relief, preserve fader bitmap, live button state, color-coded identity and actual LED level indicators. Avoid wide gradients, worn/aged brushed streaks and huge glows.

Acceptance: 8 fold combinations × 1366×768, 1666×888, owner screenshot 1711×1005, 1920×1080; compare CH+MASTER top/height with left rail; validate 7 stable lower decks and 111 only exception, WIRE overlay visibility and 4 FX rows in all-expanded. Save 1711px screenshots default, all upper expanded and all folded. Source owned by PR #278, public snapshot byte-identical. **No native audio claim.**
