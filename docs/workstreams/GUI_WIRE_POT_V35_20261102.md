# V35 — Native WIRE-pot SVG / Accepted Send Control

Scope: GUI #271 (`work/operator-console-v6`) and authoritative SVG from #278 (same user-uploaded `knob-ICON.svg`). The GUI reference alone has **no real DSP**; only accepted native ChannelId sends may color the native knob.

## Delivered
- Original uploaded SVG path is present at `web/assets/knob-ICON.svg`, with the same silhouette inlined as URL-encoded CSS mask for **offline standalone/WebView2 byte parity**. Old CSS radial knob and separate tick removed from native send control.
- **Dark grey `#4b535b`** whenever no accepted native send exists. A channel name selection, pointer hover, UI demo, or unaccepted pending request cannot light the knob.
- A row representing an accepted ChannelId `sends[]` link becomes `is-connected`. Its fill uses **native destination channel.color**; when native metadata is missing or invalid, grey is deliberately retained, never fabricated.
- Per-link 0.1dB SEND gain uses a pointer-captured, relative SVG encoder. Shift uses fine 14px/dB versus normal 3px/dB; wheel and keyboard remain native range controls, double click requests unity. No idle `pointermove` listener; active pointer handlers are installed on the captured input only and cleared after completion.
- During drag only local readout moves; **one native `SET_SEND_LEVEL` intent on accepted pointerup** with `requestId`, `deviceEpoch`, `baseGraphRevision`. On ACK rejection/timeout/cancel, accepted state remains unchanged, and the native accepted snapshot drives repaint.
- Core audio graph code, VST3 engine, stream callback, ASIO/PCM, UI channel ownership, visual reference PR278, and public Pages remain unaffected.

## Verification and remaining release boundary
- `tests/operator_console_v6_smoke.py`: Chromium computed CSS mask/fill, idle state, accepted source→destination color, pointer drag one-intent + ACK-rejection rollback, pointercancel no intent, native keyboard step.
- `tests/mixer_shell_geometry_smoke.py`: original strip/meter/fader MAIN geometry; all existing UI tests, JS syntax, v8 fixture and offline embedding gated by `.github/workflows/ui-smoke.yml`.
- `node --check web/mixer-layout.js` and `python scripts/build_ui_css.py --check` required.
- **Stop ship remains:** PR271 is a draft GUI branch. Full WIRE OUT→IN routed audio, unrelated input-channel patch destinations, disconnect/replug and Show/SoF need corresponding accepted native operations from ENGINE #266; cannot be fabricated by changing JS. Commit must be integrated with ENGINE and FAT #270 with one exact-SHA Windows runtime/physical I/O test, and crackle issue #15 still unresolved.
