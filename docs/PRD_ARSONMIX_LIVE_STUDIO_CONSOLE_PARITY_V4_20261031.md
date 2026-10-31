# ArSonMiX PRD v4.1 — Live Performance VST Host Mixer / Studio One Console parity
Updated: 2026-11-01. Product contract for #170, #192, #95, #173–180. **Binding product target, not an assertion that features already work.**


## v4.1 corrective release contract — 2026-11-01 (normative; overrides stale v4 wording below)

**Evidence baseline:** \`main@9f66d085\` includes merged FAST2 #259 + FAST3 (\`f62e9d65\`) **ONE AUDIO I/O APPLY, no START AUDIO**, #258 true independent stereo peak/FX entry, #260 atomic stereo PAN/fader targets, and #261 unified native FX Console (\`cf05c5ff\`). UI CI passed on merged #261; Windows combined CI had not completed at this audit. This remains fixed **10 input + 4 internal stereo AUX** inside the shipping callback, including a special CH1 legacy VST lease when occupied. \`DynamicChannelRoster\` and \`DynamicPreparedMixerBlock\` prove expandable ChannelId PCM off-live; #263 is a **draft restricted one-GROUP dry-only pilot**, not production support for FX/AUX/monitor/Show with dynamic buses. #251 WASAPI de-click is default-OFF experimental. Zero-FX real-hardware issue #15 remains OPEN. Inspect [live reconciliation](RECONCILED_PROGRESS_ARSONMIX_20261101.md) before claiming progress.

### Product thesis — live mixer, not audio workstation
An operator should be able to use **exactly the same CH1..CH-N channel capabilities and workflow** for any supported microphone, playback, stream, hardware input or internal bus; choose MONO or STEREO processing separately from physical input format; insert any supported VST3, send PRE/POST, direct to a new GROUP or FX RETURN, mix to MAIN and independently patch legal MONITOR/secondary hardware/software outputs. Do not impose OBS, VB-Cable, a specific interface, DAW timeline or software file recorder. N is runtime-configured subject to explicit measured device/CPU/memory/plugin resource admission, **not** a promise of physically infinite zero-cost channels. Full source-to-audible-output behavior, not an attractive channel strip, is the deliverable.

### Golden user journeys to replace feature-by-feature micro-progress
**W1 — one APPLY / physical first sound.** Choose verified ASIO or Windows Audio capture, device rate/buffer and actual output pins in AUDIO I/O; APPLY performs the required safe MUTE → device/patch accepted-revision ACK → guarded output ARM as one operator action; emergency MUTE always available. It must never silently re-arm after hotplug or failure. A visible accepted routing badge is not proof of audio: RAW PCM, post-fader PCM and actual MAIN PCM are separate diagnostics. Pull channel fader to −∞, mute channel, and mute MAIN: sound must cease, otherwise investigate direct-monitor/Windows Listen parallel routing. Never restore a redundant START AUDIO step.

**W2 — universal channel, no CH1 privilege.** \`+ CHANNEL → MONO/STEREO\` creates a stable new ChannelId, silent/unpatched by default, without borrowing an existing physical strip, and exposes the same verified input selection, mono/stereo processing, FX, send, trim, fader, pan/balance, mute/solo, meter and Show behavior available to CH1. CH1, CH11 and last-admitted CH-N must pass the same tests; no special-case UI/controller/DSP. A bus has no physical preamp only because its role differs, not because it uses a weaker insert engine.

**W3 — GROUP without losing an existing live mix.** Select any two currently active channels, already containing VST3 inserts, AUX sends and MONITOR routing; choose Add Group for Selected. Exactly one new independent GROUP ChannelId is created; selected primary OUT destinations change to GROUP, GROUP → MAIN, original parallel sends, installed plugin identity/state, device pins, MONITOR and old Show state survive. No double-dry, no need to clear effects or dismantle a normal session to create Group. GROUP receives identical mixer insert/fader/meter/mono/stereo capabilities. A dry-only restricted Group experiment does **not** meet W3.

**W4 — genuine independent FX RETURN.** Select channels and add a SEND to a new FX Return containing one real verified VST3 (e.g., reverb). Dry primary route remains unchanged, PRE/POST taps follow documented DSP order, wet return has independent native fader/meters and legal output. A drag/drop that creates only a JS card, an AUX label or a borrowed input CH is rejected as feature completion.

**W5 — Show and hardware resilience.** Save 2 mono + 1 stereo + 1 GROUP + 1 FX RETURN with actual VST3 vendor chunks, send taps, logical named I/O and accepted routing; restore preserving stable IDs and current plugin states. A missing device, invalid plugin layout, stale revision, output-pin change or corrupted chunk must reject atomically or require deliberate remap/ARM; never silently route to an unexpected speaker.

**W6 — continuous live operation.** Fader, pan, mute, solo and continuous send controls operate without driver reopening, plugin re-instantiation or global audio mute; structural changes use off-RT prepared transactions with explicit MUTE only when safety preconditions demand it. A whole-mixer dropout during ordinary fader movement is FAIL.

### What counts as a *meaningful* milestone
A milestone is **DONE** only if its observable new user journey works through **native PCM in the existing single callback**, has negative/recovery and Show/FX preservation tests, passes exact-head software checks and has a labeled hardware FAT candidate. Software PASS is not audible hardware PASS. Mark each capability as \`LIVE + CI\`, \`FAT-PENDING\`, \`OFFLINE-ONLY\`, \`DRAFT-PILOT\` or \`NOT BUILT\`; do not use "ready"/"parity" for any combined capability until all required dependencies qualify.

**Execution priority:** (P0 in parallel) isolate 0FX hardware crackle/direct-monitor bypass, never call #251 a proven fix; (M-U1) one **full-fidelity ChannelId production cutover** retaining old AUX/FX/MONITOR/Show and CH1 Orbit; (M-U2) a usable **Add Mono/Stereo + Group + FX Return** operator workflow end-to-end; (M-U3) native MASTER FX, PDC, Cue/Listen, then Scenes/VCA. Do not deliver Group that only works after deleting existing FX, or an Add Channel that only exists in a browser mock. Keep compact iGPU-friendly L2 faders, non-blurred controls, original ArSonMix visual identity.

**Benchmark evidence, feature pattern only:** [Fender Studio Pro Console](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm), [Studio Pro inserts and sends](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm), [Studio Pro software I/O matrix](https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm), [Adobe Audition bus/send/routing](https://helpx.adobe.com/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html), [Waves eMotion LV1 live patch modes](https://www.waves.com/emotion-lv1-tutorial-2-2-using-the-patch-window). These references describe their **operator-visible features**, not license to reuse proprietary art/DSP or copy internal architecture.

---

## Product identity and exclusion boundary
ArSonMiX is a **realtime Windows-first live-performance VST3 host and digital mixer** for stage/PA, live karaoke, podcasts, broadcast, radio, livestream, and any user-selected supported physical/virtual audio endpoint. It provides the breadth and speed of the **Studio One / Fender Studio Pro Console mixer**, WITHOUT adopting the DAW's timeline, arrangement, sample editor, MIDI event editor, destructive audio editing, multitrack recording, render/bounce/freeze/export session workflow, mastering timeline, or track-based transport. A virtual cable, OBS, Bluetooth, radio endpoint, and hardware speakers are choices of the operator, never default product mandates.

**Console-parity means real functional workflow and audible DSP, not a copy of proprietary source, design assets, branding, or plugin formats.** Keep the ArSonMiX compact premium brushed-gunmetal visual identity. "Zero latency" is a low-extra-DSP-latency engineering aspiration, **not** physically zero device/driver/end-to-end latency.

Official functional references (consult actual source, never speculate about internals):
- Console & channel types, in/out, banks, select, copy/paste: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm
- FX, sends, sidechain, nested buses and Add Bus for Selected: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm
- Channel Overview/Editor, Macro Controls, serial/parallel Splitter: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Channel%20Overview%20and%20Editor.htm
- Software I/O matrix, device-scoped portability, input/output configs: https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm
- VCA: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/VCA_Channels.htm
- Scoped Scenes: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Scenes.htm
- Buffer, low-latency monitor, measured latency: https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Set_Up_Your_Audio_Device.htm

## User journeys — acceptance before "parity" label
**J1 — First sound in one surface.** AUDIO SETUP: select actual installed ASIO or Windows Audio backend; explicit compatible input/output endpoints, sample rate/buffer and enabled pins; show physical capture→logical input→mixer channel→MAIN→verified hardware output, PCM/route/output status. One AUDIO I/O APPLY stages the verified configuration, waits for device/patch native ACK and performs guarded ARM under the accepted FAST2/FAST3 safety contract; emergency MUTE remains visible. Panic MUTE always visible. Do not fake signals or rearm after reconnect.

**J2 — Independent channel.** Add Mono or Stereo Channel creates a new stable ChannelId, one logical processing strip with name/color/icon, physical/software I/O SOURCE, TRIM/polarity, HPF as genuine DSP when present, mono/stereo processing format independent of source pin count, EQ/VST3 inserts, send targets, primary OUT, fader/pan/mute/solo, true per-channel meter. Default new channel unpatched and silent. Unlimited visual count is not promised: resource admission decides runtime capacity; **no hardcoded 10CH or borrowing output/input pins**.

**J3 — Group selected sources atomically.** Ctrl/Shift select MIC1/MIC2/MIC3, right-click **Add Bus for Selected Channels**, new independent *GROUP BUS* ChannelId named Vocal Bus. Their exclusive primary OUT changes to Vocal Bus; bus primary OUT goes MAIN. Existing parallel sends, source input, plugin state, relative levels persist. Single atomic native transaction with revision ACK and no doubled dry signal. "Remove Bus" supports an explicit safe rehome strategy/undo.

**J4 — Parallel FX in one gesture.** Drop reverb VST3 to selected SEND or choose Add FX Channel: create independent *FX RETURN* with owned plugin lease, create POST send from selected channel(s), keep original primary OUT unchanged. FX return routes to MAIN or other legal output; operator controls wet/dry. Send PRE/POST, level, pan follow/independent controls, copy/move/send bank work. Sidechain sends are typed separately and require compatible plugin bus ports.

**J5 — Read and edit signal path without page hunting.** Every strip exposes clickable IN, primary OUT, SEND(s) and role; destination list includes legal Bus/FX/Main/output, NONE and SEND ONLY where appropriate. Bus/FX channel shows sender count and popover with Select All / Show/Hide Sources. A selected bus highlights accepted feed/return links. Group/FX/VCA are visibly different roles.

**J6 — Native FX chain parity.** CH1..N, Group, FX Return, MAIN and any user-created physical sub-output supported by design share one native VST3 lease/insert command API, true vendor plugin editor, drag reorder, bypass, undo/copy/paste, channel FX chain presets, multi-selection paste, safe plugin state save/restore, accurate per-plugin latency. For stereo/mono/sidechain/parallel routing, enforce typed plugin bus layouts and an explicit PDC policy. **A disabled/visual Master FX button does not count.**

**J7 — Live operation and monitoring.** Fader, mute, pan, POST send gain, AUX gain, VCA/link-group fader and applicable plugin automation parameters run RT-safe, without device restart or mandatory global mute. Monitor/Cue/PFL/AFL/Listen can use independently assigned safe physical outputs. Solo-in-place and Solo-via-Listen mode are explicit; neither silently modifies MAIN when Listen is selected. Dim/talkback where supported must be deliberate.

**J8 — Hardware independent of mixer graph.** Per-device software Mono/Stereo input/output names map to driver-reported hardware pins (M/L/R); names and channel routes survive hardware replacement via *explicit review/remap*. Supports real independent MAIN/MONITOR/CUE/sub-outs and optional secondary physical/virtual playback with clock-domain policy; no blanket arbitrary independent-capture claim. Save/load/import/export I/O profiles. Negative: no phantom ports, endpoint mismatch or auto-unmute.

**J9 — Console organization.** Narrow/Compact/Expanded strips, pinned MASTER, optional Bus/FX/VCA banks on right, channel search/visibility filter, role filters, color identification, channel reorder preserving stable IDs, selected-only focus/editor, keyboard navigability, copy/paste for one/multiple channels, deterministic undo for control edits. Browser window and standalone remain coherent, but only native runtime claims audible effects.

**J10 — Channel overview and advanced processing.** One compact dockable/floating Channel Editor with actual input, I/O, Insert, SEND and CUE panels; visual FX routing (serial + bounded parallel Splitter with independent path gain and optional frequency split only when DSP really exists), macro controls bound to VST3 parameters, searchable real plugin catalog. Plugin not installed or rejected must appear disabled with reason.

**J11 — Show/Scene for live event.** Full native session with dynamic IDs, fader, routing, plugin state, output setup and device profile references. Named Scenes support scoped recall toggles (Visibility, Volume, Pan, Mute, Inserts, Sends, Cue, Input Controls, selected channels, Output Controls) plus Recall Safe and dry-run diff. Device change, sound restart, output ARM, vendor chunk failure cannot silently switch active speaker or leave partial live graph. Persistent copy/paste and presets include authenticated plugin identity.

**J11b — Live instrument and external-aux sources (advanced Console parity).** Optional VST3 instrument or external synthesizer *audio output* may become a channel input or Aux source with an independently validated plugin/instrument instance, actual PCM and native metering. If supported, real-time MIDI controller input may drive a live VSTi; this does **not** add MIDI note arrangement, sequencing, recording or an instrument-track timeline. This is a future separate admission gate, never mock Instrument strips or promise installed VSTi support from an audio-FX-only host. Reference: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm

**J12 — Onstage safety and measurable performance.** Existing routed session must remain audible when only browsing, inspecting, changing UI panel, or dragging a fader. Fail closed for stale revision, invalid plugin, unsafe device, feedback or resource budget. No callback memory allocation, blocking, OS enumeration or WebView code. On low-end Windows iGPU, responsive pointer/faders and truth-based meters; no blur filters/glow or full-strip DOM remount per meter tick. Physical no-FX crackle #15 is a release blocker.

## Mixer feature scope matrix
| Feature family | Required live parity | Explicit exclusion / distinction |
| --- | --- | --- |
| Inputs and channel types | Mono/stereo software input, audio channel, Group Bus, FX Return, Aux input/return, Main/Sub Out, Listen/Cue, VCA/control Group | Recording/Instrument-track creation and MIDI arranger |
| Routing | IN/OUT, matrix M/L/R, primary OUT, multi-out hardware, PRE/POST sends, fan-in, nested safe DAG, sidechain and parallel FX | Session offline mixdown/bounce |
| FX | Native VST3 inserts, FX chains/presets, bypass/reorder/editor, sidechain, parallel Splitter, PDC, external hardware insert if measured and safe | Proprietary Studio Pro Mix Engine FX binary compatibility; use independent original bus-processing designs |
| Monitor | Listen Bus, PFL/AFL, solo modes, Cue mixes, Dim, mono check, safe talkback and user-selected physical outputs | DAW record monitor/track arm workflow unless used purely for live input monitoring |
| Scenes | Scoped scene recall, Recall Safe, plugin chunks, I/O profiles, portable show | Timeline-based automation lanes, song arrangement |
| Controls | Link Groups, VCA, channel macro/plugin remote parameters, MIDI/OSC/control surface if implemented and tested | MIDI note sequencing / composing |
| Diagnostics | True input/output PCM meters, clip, callback xrun/clock, plugin CPU/deadline, real latency readback | Synthetic metrics presented as native telemetry |

## Priorities / acceptance gates
**Gate P0-0 hardware reliability:** First Sound on real ASIO and WASAPI, correct capture and 0-FX audible MAIN; isolate #15 and prove real hardware playback. Dev CI is necessary but not physical evidence.
**Gate P0-1 native dynamic cutover:** replace the old 10CH renderer (not run it in parallel) after exact legacy PCM, VST/Orbit, Show, physical pin/monitor/secondary parity. At least 12 independently addressed mono/stereo logical channels + a real independent Group + FX Return demonstrated *inside the existing callback*.
**Gate P0-2 daily console:** J2–J6 end-to-end under real native ACK, no borrowed channel, no false green LIVE. Physical test route two mics to a new Group with active VST.
**Gate P1 FX/Listen/PDC:** comprehensive VST3 including MAIN, Cue/Listen, strict latency classification/compensation, scene safety.
**Gate P1 UX/portable:** J8–J11 + 1366×768/standard iGPU; progressive complexity by user role.
**Gate P2 extended parity:** VCA Groups, macro mapping, parallel FX Splitter, hardware insert and optional surround/multichannel only after engine+resource/format admission. Multichannel beyond stereo and third-party control-surface support are independently gated; do not fake.

A feature is DONE only after **native source + executable tests + GUI operator scenario + successful exact-head integration CI + physical FAT where audio/hardware is involved**. Standalone mock UI, offline PCM and green CI alone are NOT a release claim.

## References / related implementation owner
[ARD v4](ARD_ARSONMIX_LIVE_STUDIO_CONSOLE_PARITY_V4_20261031.md) · [GUI spec v4](GUI_ARSONMIX_STUDIO_CONSOLE_V4_20261031.md) · [M4+ handoff](HANDOFF_ARSONMIX_STUDIO_CONSOLE_V4_20261031.md) · [feature ledger](STUDIO_CONSOLE_PARITY_MATRIX_V4_20261031.md).
Historical v3 PRD/ARD remain background (not the final parity scope); refresh main and open PRs before editing audio.
