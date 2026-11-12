# ARD — Native Contextual Console, Digital Gain, and Per-Edge Sends on Faders

**Decision date:** 2026-11-13. **Status:** NORMATIVE TARGET / implementation contract, **not** native-feature completion, public release, or physical FAT. **Companion:** [Owner-approved contextual PRD](PRD_CONTEXTUAL_CONSOLE_GAIN_SOF_20261113.md).

**Scope precedence.** For contextual GAIN/FX RACK, SOF, send ON/OFF and PRE/POST behavior this document supersedes conflicting sections of the historical [ARD v5.2](ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md) and [MS5](MS5_NATIVE_AUX_SENDS_ON_FADERS_20261031.md). It preserves the [WIRE-first architecture](ARD_WIRE_FIRST_ZERO_ADDED_LATENCY_20261104.md), [single approved GUI](GITHUB_PAGES_PREVIEW.md), [root AGENTS](../AGENTS.md), current physical MUTE/SOLO/PRE/POST PNG material, audio/device safety, native PCM graph and approved plugin lifecycle. This specification must not become a duplicate JS engine or independent audio callback.

## 0. The architectural invariant

\`\`\`text
Approved assembled /preview/ (one DOM; visual/intent only)
   -> web/approved-native-bridge.js + web/approved-native-contract.js
   -> src/Main.cpp native message/CONTROL dispatch
   -> src/NativeLogicalMixerControl.h / NativeLogicalMixerWorkspace.h
   -> accepted MixerSessionDocument with stable ChannelId, EdgeId, InsertId
   -> CONTROL: validation, DSP parameter staging / prepared-graph compilation
   -> DynamicNativeSessionPublisher + existing realtime route publisher
   -> ONE JUCE AudioIODeviceCallback / installed PCM graph
   -> installed ACK, graph/device epoch + actual accepted readback -> same DOM
\`\`\`

A logical-only offline edit, a retained graph, an installed graph and audible verified hardware are **different evidence levels**. Browser \`/preview/\` renders a **demonstration**. No CSS key frame, local \`wireEdges\`, simulated meter, 250ms polling result or optimistic DOM value may claim actual PCM.

## 1. Ownership map (reuse existing seams, no substitute console)

| Concern | Existing owners / entry points | Required behavior |
| --- | --- | --- |
| One GUI, layout modes | \`site/preview-v2/index.html\` is **internal build input**, \`site/preview-v2/assets/mixer-section-controls.css\`, approved PNG key CSS/atlas, \`scripts/build_pages.py\`, \`scripts/build_approved_native_gui.py\` | Build **only** approved \`/preview/\` for Pages and native; \`/preview-v2/\` is not a separate product GUI. View-only state separate from PCM model. Preserve channel/fader/master/physical WIRE geometry and artwork. |
| Native UI truth and transport | \`web/approved-native-bridge.js\`, \`web/approved-native-contract.js\`, \`integration/ApprovedNativeControlIntent.h\`, native host \`src/Main.cpp\` | Typed capability + requestId + graph revision + device epoch, rejected command feedback; only accepted value after correlated native ACK/readback. |
| Logical mixer, save/restore | \`src/NativeLogicalMixerControl.h\`, \`src/NativeLogicalMixerWorkspace.h\`, \`src/NativeMixerSessionDocument.h\`, \`src/NativeMixerSessionStore.h\`, \`engine/core/DynamicChannelRoster.h\` | Authoritative persisted ChannelId digital gain and EdgeId tap/level/enabled. Preserve old session data and stable IDs; schema migration, not shadow routing. |
| PCM and FX | \`engine/core/DynamicPreparedMixerBlock.h\`, \`engine/core/PreparedMixerBlock.h\`, \`engine/core/DynamicNativeSessionPublisher.h\`, \`src/LiveAudioEngine.h\`, \`src/LiveDryRoutingLab.h\` compatibility | Actual pre/post tap and parameter smoothing in one existing callback. Keep plugin lease processing once per routed channel/bus and preserve reported FX latency/PDC. |
| UI + PCM tests | \`tests/sof_contextual_console_smoke.py\` (in #604/#606 until merged), existing WIRE/publisher tests; nearest native PCM/bridge/session fixtures | Test distinct presentation and installed audio, not a text-only source assertion. No redundant Windows/installer per docs/UI edit. |

The file list is an entrypoint, not a command to edit every file. Split large handlers only when replacing redundant responsibility with a tested single owner, not by layering new wrappers or independent state machines.

## 2. State model — presentation is not routing

\`\`\`text
ViewContext {
    mixerMode: MAIN_MIX | SENDS_ON_FADERS;     // ephemeral presentation
    mainStripPane: FX_RACK | INPUT_GAIN;       // remembered while SOF active
    selectedSendDestination: ChannelId?;      // stable ID, not name
    selectedWireEdge: EdgeId?;
}
ChannelState { ChannelId, inputGainDb, faderGain, pan, mute, solo, inserts, ... }
SendEdge { EdgeId, source:ChannelId, destination:ChannelId,
           role:PRIMARY_OUT|PARALLEL_SEND, tap:POST_FADER|PRE_FADER|PRE_FX,
           gain, sendPan, enabled, ... }
\`\`\`

**This is a target conceptual schema, not a claim these fields/enum strings exist today.** Expose only capabilities implemented by the installed native session. The existing browser \`wireTapMode=send|split\` and EdgeId \`mode=send|split\` are *legacy reference representations*, not proof of production PRE_FADER. \`split\` currently means PRE-FX in the preview, so migration must preserve meaning in saved sessions and not silently reinterpret it.

Presentation changes (\`GAIN\` vs \`FX RACK\`, SOF enter/exit, selecting BUS, bank/paging, density and panel collapse) are **view-only commands**. They may re-render only affected controls, but must not issue a WIRE transaction, DSP/FX create/destroy, device apply or audio rebind. Persist actual DSP parameters in \`.smix\`; optional UI-view preference may live separately and must never gate startup sound.

**Identity / topology:**
- \`ChannelId\`, \`EdgeId\`, \`InsertId\`, graph revision, requestId and device epoch form identity/ordering. Display name, strip ordinal, array index, AUX number and current bank are not identities.
- \`PRIMARY_OUT\` is one exclusive main route to MAIN/GROUP and follows the channel's normal fader semantics; it is **not** editable as a parallel PRE/POST send. \`PARALLEL_SEND\` may target only validated legal destinations without self/cycle/double-feed; established WIRE graph legality prevails.
- Selected source+target resolves one **accepted** parallel EdgeId; if no such edge, present \`NO SEND / Patch with WIRE\` and disable send gain/tap. Never auto-connect from send fader motion, nor auto-promote a primary route into a send.
- When a selected channel has several outgoing sends, an old channel-wide PRE/POST physical key cannot modify them all: use explicit selected EdgeId or show \`MIX\` and require selection. In SOF context the PRE/POST key is *per selected target*.

## 3. Exact native signal graph and tap naming

\`\`\`text
Actual physical/software capture (verified source/pin/epoch)
      |
      v
Source decode / mono-stereo ingress / polarity
      |
      v
DIGITAL INPUT GAIN [one ChannelId gain stage; never device mic-preamp]
      |
      +---- PRE_FX tap [only if separate legitimate capability is admitted]
      |
      v
Ordered internal FX + external VST3 inserts [actual DSP, known PDC]
      |
      +---- PRE_FADER tap --- independent send LEVEL/PAN/ENABLED ---> legal BUS/FX target
      |       (after FX; before channel fader)
      v
Channel FADER + channel PAN/BALANCE
      |
      +---- POST_FADER tap -- independent send LEVEL/PAN/ENABLED ---> legal BUS/FX target
      |
      v
PRIMARY_OUT (GROUP / MAIN) -> bus FX/fader/OUTPUT -> verified device
\`\`\`

This diagram is conceptual **channel-stage order**, not a second processing pass or an extra physical output path. \`PRE_FADER\` **must** carry fully processed inserts and not follow the source fader. \`POST_FADER\` **must** follow source fader and the documented pan/balance policy. \`PRE_FX\` is distinctly different and may be supported as an explicit advanced tap, not relabeled \`PRE\`. POST is the default for a newly created independent SEND unless an operator has deliberately chosen a different admitted tap.

**MUTE rule:** Product safety policy for new qualified behavior is channel MUTE gates PRE and POST parallel sends as well as primary contribution, with deliberate click-free ramp. PRE ignores *fader position*, not MUTE. SOLO policy must preserve existing acknowledged live solo/monitor behavior and must be fixture-tested rather than silently rebuilt during this feature. Explicit channel mute/unmute or device loss must not be bypassed by SOF or by PRE wiring. Any legacy session compatibility distinction must be audited and migrated without changing old audio secretly.

**Stereo/PAN:** For mono sources, compute legal mono->stereo projection using the destination/edge's defined pan law; for stereo channels preserve L/R, avoid unintended doubling or accidental 6dB dry mix. Per-send PAN is independent if admitted; do not falsely display a pan value without native support. Never insert a second plugin execution just to produce PRE and POST taps. Internal routing can add zero pipeline samples *only when the actual plugin/PDC/device latency permits*; avoid claiming 0ms absolute roundtrip.

## 4. Commands, synchronization, idempotence

These operation names are **proposed typed design names**; implement/reuse equivalent existing canonical commands if they exist, and do NOT expose names to clients before the capability snapshot advertises them.

| Gesture | Target typed operation | Preconditions, postconditions |
| --- | --- | --- |
| View \`GAIN\` or \`FX RACK\` | UI-local \`SetStripPane\` | Pure view; preserve plugin state; bypass and FX UI unchanged |
| SOF enter/leave; target BUS selection | UI-local \`SetMixerMode\`, \`SelectSendDestination\` | Source/dest stable IDs; no DSP or route mutation |
| Move Input Gain knob / numeric entry | \`SET_CHANNEL_INPUT_GAIN\` (ChannelId, gainDb) | Prepare parameter for existing channel pre-insert gain; clamp only to advertised exact range; ACK native applied value + channel/graph revision; reject stale/invalid without optimistic audio claim |
| Drag a send fader | \`SET_SEND_LEVEL\` (EdgeId, gainDb/linear) | Parallel send exists, destination legal; only one edge gain changes; no \`CONNECT\`, \`REMOVE\`, \`ROUTE_ONLY\`, tap reset, or primary fader mutation |
| Toggle SEND ON/OFF | \`SET_SEND_ENABLED\` (EdgeId, bool) | OFF sets contribution to 0 but retains EdgeId, gain, tap, pan and destination; ON restores; no WIRE removal |
| Toggle PRE/POST | \`SET_SEND_TAP\` (EdgeId, PRE_FADER|POST_FADER) | One selected EdgeId; structural tap change off callback; validated new graph, atomic publish and ACK/readback; never use legacy PRE_FX as shorthand |
| Patch/remove/retarget a WIRE | Existing explicit \`WIRE_TRANSACTION\` contract | Operator-initiated topology edit, cycle/headroom checks, same stable EdgeId on replug as permitted |

All native intents carry **requestId**, **expected logical/graph revision** and proper ChannelId/EdgeId; include **device epoch** when actual installed PCM/device binding is concerned. Validate identities, capability, finiteness, legal dB/linear representation, duplicate/replayed requests, expected graph version, role and destination before staging.

**Reply categories:** \`REJECTED\` (no accepted state change), \`LOGICAL_ONLY\` (durable offline state, no audio claim), \`RETAINED\` (not installed PCM), and \`INSTALLED/ACKED\` (matching installed graph revision and parameter/tap). UI must match accepted snapshot; late/stale ACK cannot overwrite newer selection. Never silently present LOGICAL_ONLY as active sound; provide a compact status hint rather than noisy error text.

**Realtime parameter path:** For gain/level/enabled on an unchanged edge, prefer a bounded preprepared parameter mailbox / existing DSP ramp with no graph rebuild per pointer movement. Coalesce GUI updates by target revision so wheel/drag does not flood CONTROL or trigger full bank DOM render; do not postpone **all** audible response to mouseup if it makes live mixing unusable. Apply click-free smoothing to gain changes and send OFF; preserve correct final accepted value. For actual tap/topology edits, prepare off-audio-thread immutable graph, verify cycles and plugin order, publish atomically at safe callback boundary, retire only after callback acknowledgment. Never allocate, lock, scan VST, log, await device, or construct JSON in the realtime callback.

**Fader floor is a special case:** Browser reference may display \`−60 dB\` as minimum, but \(10^{-60/20}=0.001\), **not silence**. Define one *explicit* \`-∞ / OFF / zero-linear\` representation at the physical fader stop (or the negotiated minimum) so the DSP actually receives zero; do **not** map numeric \`−60 dB\` to 0.001 and assert silence. Most importantly, **never** convert this control value into a WIRE \`REMOVE\` operation. Release/ramp once, retain EdgeId.

## 5. Native migration / current state gaps

**Checkpoint checked on 2026-11-13** (refresh live before coding): main \`6309d79b5ff8e873e64326f5319c177aba118235\`. [#604](https://github.com/masarray/arsonmix/pull/604) is draft browser contextual-GAIN/SOF prototype; [#606](https://github.com/masarray/arsonmix/pull/606) is the **more advanced** draft including #604 and SonKu source. Neither is certified as merged or release-quality in this checkpoint.

- Existing #604/#606 reference implementation has \`gainView/fxView\`, \`stripView\`, \`sectionGain\` and \`sectionSof\`, per-EdgeId PRE action, protected SOF gain, disabled absent edge, toolbar \`SEND MIX\` label, and owner-approved PNG sprite. **Reuse** this work if still live, not a competing rewrite.
- Present source: \`sectionGain\` in browser demonstrates \`t.trim\` range and LCD, but native input Gain is marked **pending**; native bridge hides/blocks unsupported demo-only actions. An *actual* native digital input-gain value and PCM node are required before enabling it.
- Present source: \`sectionSof\` PRE currently maps browser \`mode='split'\` to **PRE_FX**, not native post-insert PRE_FADER. Native \`SET_SEND_TAP\` graph transaction and truthful PRE_FADER readback are required, with migration for existing split edges.
- Present source: browser \`getSofEdge\` resolves an exact parallel edge; \`setSofLevel\` no longer removes a WIRE at minimum and will not auto-create a missing edge. Native per-send level and enabled commands still need capability and installed readback.
- Native bridge currently suppresses \`sofToggle/sofTarget\` as reference-only until acknowledged native send-gain routing exists. That is **honest**, but not a complete production SOF. No pseudo-native UI that works only with browser-local \`wireEdges\`.
- \`DynamicChannelRoster.h\`, \`NativeLogicalMixerWorkspace.h\`, \`ApprovedNativeControlIntent.h\`, \`PreparedMixerBlock.h\` already contain pieces of pre/post send/parameter mechanics; audit exact processing point, mute/solo, graph transactions and native coverage before adding new state. Do not create duplicate DSP support.

## 6. Implementation order — one meaningful vertical milestone

**M1: capability and genuine PCM contract.** Add/extend minimal canonical ChannelId input gain, EdgeId send level/ON/tap with saved doc schema, exact source/return staging, pre-fader after insert split, and accepted native graph snapshot; wire via existing transport/bridge. One integrated branch based on latest surviving in-scope PR (#606 includes #604 as of checkpoint); verify live refs first, do not overwrite another agent's head.

**M2: contextual UI admission.** Reuse the current approved renderer and artwork from that branch, switch only relevant FX section contents, give SOF clear BUS target and accepted PRE/POST/ON state, re-enable controls only after actual native capability. MAIN fader, pinned MAIN, meter, WIRE and real plugin editor unchanged. No heavy additional layout system.

**M3: focused qualification, then release gate.** Cross-check output with real PCM and .smix fixtures before one deliberate Windows milestone/FAT. This is *not* permission to perform heavy Windows/Chromium/installer builds on every docs or CSS tweak. Follow [fast feature playbook](architecture/FEATURE_EXTENSION_PLAYBOOK.md) and [smart CI runbook](SMART_CI_RUNNER_WORKFLOW.md).

## 7. Specific fixture matrix

1. **Gain order:** input 0, +6, negative gain, unity reset; capture -> gain -> real EQ/VST -> fader -> MAIN; clipping/headroom and mute; device reconnect, mono/stereo. Changing view must not reset FX state.
2. **Post-insert PRE:** deterministic nonlinear/known FX changes PRE sample as expected, while source channel fader at zero does not attenuate PRE; POST is silent. \`PRE_FX\` optional tap yields intentionally different PCM. Plugin processing is done once.
3. **PRE / POST + MUTE:** mute gates both under new behavior; explicit solo/monitor policies and intentional SOURCE ONLY remain correct, no leak into MAIN.
4. **Two simultaneous sends:** CH05 -> Monitor PRE and CH05 -> Reverb POST; only selected EdgeId changes; target change and bank swap do not retarget any audio.
5. **OFF / gain floor / remove:** send OFF and level floor silence PCM but retain EdgeId/settings; ON and fader restore. Only explicit Disconnect changes topology; primary route never converted/duplicated. Test from absent edge and primary-only path.
6. **Failure + concurrency:** stale revision/epoch, rapid gain drag, toggle+device restart, rejected invalid operation, late ACK, plugin open/close, graph retirement. No stale optimistic badge or callback allocation.
7. **Persistence:** .smix serializes ChannelId input gain and per-EdgeId tap/gain/enabled; legacy \`split\` migrates as PRE_FX, not silently PRE_FADER; optional UI mode does not become startup audio dependency.
8. **Viewport and genuine look:** assembled \`/preview/\` at 1366×768, 1680×900, 1920×1080 and relevant compact viewport; approved PNG visual states MUTE/SOLO/PRE/POST, native target context, equal-height sections, MASTER pinned, no new spinner/wizard, no console-wide idle repaint.
9. **Real external signal:** ASIO input -> input gain -> channel FX/VST3 -> PRE and POST bus -> verified outputs; secondary WASAPI/VB Cable independent; no unadmitted audio, hidden arming, or device takeover.
10. **Cost discipline:** docs-only = diff/links/contract checks, UI-only = cheap scripted smoke at milestones, PCM changes = unit and one deliberate exact-SHA native build, output/hardware changes = physical FAT; don't stack duplicate Windows runs.

**Release rule:** The feature is not "DONE" on PR merge, green source assertions, GUI screenshot or browser smoke alone. It is DONE when its exact installed native PCM behavior and failure/recovery paths match PRD and are witnessed by the appropriate evidence tier. No public release claim without normal release qualification.

## 8. Handoff instructions for the next thread

1. Read root \`AGENTS.md\`, this ARD and [PRD](PRD_CONTEXTUAL_CONSOLE_GAIN_SOF_20261113.md), architecture map and actual current source.
2. Refresh live main, #604/#606 status/heads, compare changed paths, CI and earlier integrated work. Prefer latest survivor; #606 already includes #604 at this checkpoint. Do not reimplement the same code on main blindly or overwrite active SonKu/editor progress.
3. Deliver native end-to-end feature, not a larger design proposal; keep one authoritative ChannelId/EdgeId graph, one FX lease owner and one callback.
4. Document exactly what is REAL (GUI, logical-only, installed PCM, actual hardware) and what remains blocked. Use selective efficient tests; no unsupported "released" claim.
