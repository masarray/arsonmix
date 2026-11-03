# ArSonMix — GUI-final engineering handoff (2 November 2026)

**Product decision: operator UI WORKFLOW LOCKED for ENGINE #266, not audio-runtime certified.** GUI production owner: PR #271. User-approved interaction/geometry north star: PR #278 + published WIRE V36/V37 reference; hardware material V5 supersedes old BTN-180 art. FAT owner: PR #270. Single Console, one WebView2, one native JUCE callback. No JS graph/PCM engine and no imported demo rack/meters.

## Fixed operator workflow
One dense mixer console shows **IN → M/ST → VST3 inserts → SEND/FX return → PAN/TRIM/CUE/MUTE/SOLO → FADER / genuine L/R meter → OUT**, with one-width pinned MAIN. The permanent section rail collapses I/O, FX and SEND for all strips without changing fader alignment. Source, destination, graph revision and route state must remain inspectable across banks. A WIRE source is a visual selection only; clicking outside releases it, hover affordance and enlarged IN hit target are requirements for the final patch renderer. Never display an imaginary long wire between off-screen banks. Reference WIRE model is NOT executable DSP.

**Visual lock:** lightweight graphite #0B161E/#14232C/#20313B, approved V5 physical keys with tiny semantic backlight (MUTE red, SOLO amber, PFL cyan), single SVG text knockout, original channel palette/icons where *native metadata explicitly publishes them*; top and bottom use the same accepted color and icon; no CSS blur/hover glow. MAIN and each channel use EXACT shared width at 150/125/105 px for comfort/standard/compact. No CSS-render-driven audio changes.

## One authoritative native socket
Contract remains `schemaVersion=6` in v8 orchestration. Subscribe `arMixConsoleSubscribe` → native `arMixConsoleSnapshot`, intents `arMixConsoleIntent` → native `arMixConsoleAck` → same-epoch/revision accepted snapshot. Every uint64 ID/revision is a canonical decimal **string**; requestId includes unique per-WebView nonce. GUI never replaces the accepted view with PENDING state or infers LIVE from clicks. Unknown operation, mismatched deviceEpoch/revision, missing hardware mask, unsupported source pin, callback not LIVE => disabled/rejected, original graph retained. The native producer must publish `device.{epoch,backend,captureDevice,playbackDevice,inputEndpoints,outputEndpoints,enabledInputs,enabledOutputs,sampleRate,bufferFrames,outputArmed}`, `graph.{revision,rosterRevision,channels,destinations,capabilities}`, and measured `telemetry`.

| Final operator action | Intent operation | Existing GUI payload / expected ENGINE behavior |
|---|---|---|
| Select physical input IN | SET_SOURCE | `{portId}` verified enabled endpoint for exact ChannelId; no pseudo-stereo alias |
| Select primary OUT / serial Group | SET_PRIMARY_OUT | `{destinationId}` accepted bus/physical output by role; cycle/feedback rejected |
| MONO/STEREO processing | SET_PROCESSING_FORMAT | `{format}` independent of count of genuine physical pins |
| Add/select native VST3 | ADD_INSERT / PICK_INSERT_FILE | `{pluginId}` from native catalog or native picker; actual lease and CPU voice, never mock |
| Bypass/editor/reorder VST3 | SET_INSERT_BYPASS / OPEN_INSERT_EDITOR / MOVE_INSERT | `{insertId,bypassed}`, `{insertId}`, `{insertId,beforeInsertId}`; native lease preserved |
| SEND/FX-return | ADD_SEND / SET_SEND_TAP / SET_SEND_LEVEL | `{targetId,tap:'POST',levelDb:0}`, `{sendId,tap}`, `{sendId,levelDb}`; PRE/POST real PCM, no dry duplication |
| PAN / TRIM / FADER | SET_PAN / SET_TRIM / SET_FADER | `{value}`, `{valueDb}`, `{valueDb}`; RT smoothing, not graph rebuilt on drag |
| MUTE / SOLO / PFL (Cue) | SET_MUTE / SET_SOLO / SET_CUE | `{value:boolean}`; monitor semantics separate from MAIN summing |
| Link selected faders | SET_LINK_GROUP | `{channelIds,linked}`; controls only, NOT a Group DSP bus |
| Route selected to genuine Group | ROUTE_SELECTED_TO_GROUP | `{channelIds,destinationId}`; *serial exclusive* dry path without doubles |
| Rename / create native channel | RENAME_CHANNEL / CREATE_CHANNEL | `{name}` / `{role}`; native publishes stable ChannelId and rosterRevision |
| WIRE source/plug/replug/unplug, cross-bank navigator | **NOT INSTALLED AS LIVE OP** | Ref PR #278/V37 only. ENGINE+GUI+FAT must define corresponding accepted graph transactions in shared v8 contract BEFORE enabling. Do not simulate sound. |
| Device changes, Show recall/save, independent Monitor outputs | **ENGINE operation contract pending** | Native epoch/pins and full Show/plugin chunks must be published; fail closed until shared contract supports these operations. |

### Channel identity schema extension: single-owner admission required
GUI now *reads optional* `graph.channels[].color` (#RRGGBB) and `graph.channels[].icon` (short glyph) ONLY from actual accepted native snapshots; missing data gives neutral grey and no icon, never palette-by-index. **ENGINE must add optional metadata and immutable accepted updates through issue #192 with fixture + validator + FAT tests**. Renaming keeps existing native ID; icon/color editing remains blocked until there are specific new supportedOps and ACK. Never advertise an editor that merely updates JS.

### Release truth
Current PR #271 uses an illustrative Chromium native stub for S1/S2 controls, not real callback/ASIO PCM evidence. S0 producer unready C++ trace is preserved unchanged. Real full-fidelity callback promotion, actual 2-input/2-VST3/CH11/serial Group/wet Return/Monitor/Show, exact unified SHA Windows build and hardware crackle #15 remain mandatory; no public stable installer or user UAT claim before FAT #270. One combined FAT/Windows/Chromium gate, no repeated small CI promotions.
