# PRD — Contextual Console: GAIN / FX RACK / Sends on Faders

**Product decision:** Owner-approved 2026-11-13. **Status:** NORMATIVE REQUIREMENTS, **not** implementation / native PCM / release evidence. **Companion:** [ARD — Contextual Console](ARD_CONTEXTUAL_CONSOLE_GAIN_SOF_20261113.md).

**Scope and precedence.** This addendum governs the GAIN / FX RACK view selector, Sends on Faders (SOF), send PRE/POST, send ON/OFF, source-channel digital gain, and related WIRE controls. It supersedes *conflicting UI and tap descriptions* in [PRD v5.2](PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md), [MS5](MS5_NATIVE_AUX_SENDS_ON_FADERS_20261031.md) and earlier wire preview proposals, **only for this scope**. [WIRE-first PRD](PRD_WIRE_FIRST_LIVE_ROUTING_20261104.md), [WIRE-first ARD](ARD_WIRE_FIRST_ZERO_ADDED_LATENCY_20261104.md), [AGENTS.md](../AGENTS.md), and the one approved \`/preview/\` take precedence on overall routing, UI authority, and safety. This does **not** authorize another GUI, audio engine, or independent mixer.

## 1. Operator outcome

An ordinary operator can (a) adjust a vocal channel's **digital** input gain with a knob and legible dB LCD, (b) return immediately to the existing FX rack, (c) select BUS A / FX return / another legally reachable send destination and move all contributing channels' **send** faders, (d) independently choose PRE or POST and enable/disable each send, and (e) return to the exact previous channel-fader mix **without changing the graph by viewing it**.

Optimize for a genuine live digital console: low visual noise, compact premium physical controls, no cards/panels piled onto every strip, no extra wizard, no hidden monitoring/ARM step, no forced app/device restart, and no DAW timeline. Borrow **interaction patterns** from Yamaha DM7/StageMix SENDS ON FADER, Waves eMotion LV1 Fader Flip and Layer Modes, Allen & Heath Avantis Pre Mix, and Studio One per-send controls; do not copy their proprietary pixels, branding, icon artwork, DSP or assets.

**Product naming:** ArSonMix is the mixer; **Harmonice** is an internal vocal-harmony FX/plugin, **not** the name of the entire mixer, GAIN, SOF, or spatial 8D-SonKu processor.

## 2. One approved compact mixer, two operating contexts

| Context | Toolbar / indication | Shared channel area below Input | Fader | PRE/POST object |
| --- | --- | --- | --- | --- |
| **MAIN / CHANNEL MIX** (default) | Interlocked \`GAIN | FX RACK\` selector; FX RACK initially selected | Either original full FX insert rack **or** digital input GAIN knob + numeric LCD, at **identical height/width** | Regular channel fader; pinned MAIN is normal MAIN | No hidden global send changes |
| **SENDS ON FADERS** | High-contrast \`SEND MIX → [destination]\`; dedicated target selector; conspicuous return-to-main action | **SEND CONTROLS** for this channel -> selected destination (send enabled, PRE/POST tap, status; send pan only if implemented) | Send level **only for that specific existing parallel EdgeId** | The selected **source -> selected target** send only |

- The two GAIN / FX RACK buttons are a **mutually exclusive segmented view selector**, not independent effect-power switches or per-channel DSP toggles. Position near BANK/VIEW and SOF in the original toolbar; use existing button design tokens. At least one is selected in MAIN context. Switching views must **not** mute, unload, instantiate, reorder, bypass, or reset a VST3.
- SOF temporarily supersedes the center view on eligible source strips and remembers the operator's prior GAIN/FX RACK choice. On exit, restore that view and all channel fader positions, PAN, FX order, routes and active audio. The SOF target can be switched without leaving SOF.
- Keep **the same** approved metallic mixer skins, SVG fader cap, approved MUTE/SOLO/PRE/POST hardware PNG sprite state (current approved main; later versions only after merge), physical WIRE jacks/cable, channel identity, PAN, meters, density/bank, and pinned MAIN geometry. No competing public URL, detached mixer, oversized text/cards, permanent new rows or synthetic FX/routing widgets.
- MUTE/SOLO are **channel commands in both modes**, never relabeled as send ON/OFF. MASTER remains the actual MAIN fader in either mode and is never silently flipped to the selected bus. GROUP/FX targets remain independently controllable via their actual channels, not simulated duplicate strips.
- Visual authority must be specific: MAIN fader readout vs \`SEND → BUS A −12.0 dB\`; \`NO SEND · Patch with WIRE\`; \`PRIMARY ROUTE\`; \`PRE-FADER\`; \`POST-FADER\`; \`MIX\` when viewing mixed edges. Do not show a glowing/connected send when no actual accepted edge exists.
- Standard/Compact/Narrow should avoid clipping. SOF can minimize ancillary FX text or hide inactive detail in compact density, but WIRE, faders and current pinned MAIN geometry remain present.

## 3. GAIN workflow and meaning

1. Click **GAIN** in MAIN context -> the existing FX cell becomes a small responsive physical-style **knob**, with an LCD numeric \`+0.0 dB\` beneath. Click **FX RACK** -> exactly the existing live insert list returns.
2. Knob supports coarse drag/wheel, fine adjustment (Shift), keyboard accessibility, numeric LCD direct entry and reset to unity by deliberate double-click; avoid accidental reset on ordinary click. Preserve value and keyboard focus through re-renders; prefer local control updates rather than recreating whole bank on every gesture.
3. The value is **DIGITAL INPUT GAIN** before channel inserts, not the physical microphone preamp/ASIO driver's gain, not WIRE send level, not channel fader, and not a second FX. Clearly label it \`INPUT GAIN\`; WIRE's old \`GAIN\` inspector label becomes \`SEND LEVEL\` where a send is being edited.
4. Gain is stored by stable ChannelId and survives FX view switching, session save/reopen, device reconnect, channel rename, bank/density changes and ordinary WIRE replug. Stereo processing defaults to linked L/R gain; no invented independent control unless native supports it. Other node types must not show a functional **input** gain if they have no admitted gain stage.
5. A browser mock's current trim range (\`−24 .. +24 dB\`) is **not proof** of a native gain implementation, hardware capability or public range guarantee. Confirm native representability, headroom, metering, clipping behavior and rollback before exposing an active native control.

## 4. SOF workflow and behavior

1. User presses **Sends on Faders**, selects a **verified eligible destination** BUS, FX Return or legal internal ChannelId target. The target label is unambiguous, persists while paging through banks, and displays source and destination identity; names are display labels, never persistent identity.
2. Every eligible source channel displays its own existing \`(ChannelId, EdgeId) -> destination\` send. If an edge exists and is an independent send, the channel fader can adjust its level and show the actual accepted dB value. A source with **no send** shows \`NO SEND\`; fader is inactive until the user **explicitly** patches WIRE or chooses a clear connect operation. Never auto-create a WIRE by dragging a fader.
3. **SOF fader at minimum = silence for that send**, not a route removal. Repeated low/high movements must preserve **exact EdgeId, destination, tap, pan and send enabled state**; no route duplicate, accidental dry duplication or implicit MAIN feed. WIRE remove/retarget/route-only requires separate explicit user action.
4. **SEND ON/OFF is separate** from route topology and level: OFF suppresses contribution but retains saved level, tap, pan and EdgeId; ON restores precisely the prior values. An absent send is not the same as OFF and should not appear connected. Existing mute/solo/fader and channel safety semantics must not be silently repurposed.
5. **PRE/POST is per outgoing parallel send**, not a channel-global operation. For \`VOCAL -> MONITOR\` PRE and \`VOCAL -> REVERB\` POST, changing MONITOR must leave REVERB unchanged. Show PRE/POST in the SOF **SEND CONTROLS** area next to send ON/status. Outside SOF, the existing physical PRE/POST button may function only for one unambiguously selected edge, or serve as read-only/mixed indicator when multiple edges exist. Never bulk-retarget all sends via a single ambiguous button.
6. PRE means **post-insert PRE-FADER**; POST means **post-fader** (with documented post-pan policy). **PRE-FX** is a *different* tap. Until native DSP distinguishes these and ACKs the real result, the UI must say \`PRE-FX\` where that is what it actually implements; do not rename \`split\` to PRE-FADER just to make the badge look standard. Default newly connected independent sends to POST unless explicit operator choice applies. Primary exclusive OUT to MAIN/GROUP is not an independent PRE/POST send, and cannot be converted by the SOF PRE key.
7. Mono/stereo conversion, PAN/BALANCE, polarity, channel MUTE/SOLO, solo-safe and PFL/cue remain independently defined. **Product safety target:** MUTE gates all audio contributions including PRE sends; PRE must remain independent of channel **fader**, not create a secret bypass around MUTE. Any compatibility difference needs explicit verified migration and tests; do not silently reinterpret older .smix sessions.
8. A source-to-destination pair may already have a primary route. Do not pretend that primary route is a parallel send or make a second route just for SOF. Disable SOF fader appropriately, with a truthful explanation; primary OUT is controlled by the channel fader in MAIN context.

## 5. Native truthfulness, failures and recovery

- GUI mock / approved GitHub Pages \`/preview/\` may demonstrate view switching, but **does not provide PCM**. A native control is interactive only when the underlying typed intent, actual installed graph authority and correlated ACK + accepted readback exist. Absent capability: show explicit unavailable status; **never** move/send audio speculatively. Merely hiding/demo-disable may be acceptable temporarily, never as completed public feature.
- Live session should automatically preserve the operator's trusted last-used graph and useful audio across GUI toggles and device reconnect. Do not demand a Monitor ON/ARM/rebind wizard to return to valid audible output; do not override an intentional user MUTE, SOURCE ONLY, missing output or device safety failure.
- No audio drop, VST3 reset, FX GUI close, device reset, layout jump or stale fader value when clicking GAIN/FX RACK, entering/leaving SOF, switching target, or changing page. A failed native ACK must keep the **previous accepted** value and state, with specific feedback.
- Only one authoritative native callback/PCM graph. Preview model, JS local state, \`sendSlots\` legacy objects or device ordinal must not become competing routing owners.

## 6. Acceptance criteria — must be witnessed

| Scenario | Required evidence |
| --- | --- |
| GAIN/FX mode interlock | Same FX section dimensions; UI chooses one mode, restores correctly, plugin chain and audio continue unchanged; real native gain changes measured PCM and survives reopen |
| PRE vs POST on a channel | Real PCM fixture: reduce source fader to minimum -> PRE-FADER send still outputs signal (when not muted) and POST-FADER send goes silent; channel MUTE closes both under new approved policy |
| Two destination sends | MONITOR PRE and REVERB POST, each with own levels/pan/ON; changing only one EdgeId never alters the other or primary MAIN |
| Fader minimum/off | −∞/minimum produces zero send audio, with retained EdgeId and all send settings; toggling OFF/ON restores gain; unplug is the only explicit removal |
| No send / primary-only | SOF says NO SEND or PRIMARY ROUTE; fader action cannot auto-connect, auto-remove, promote or duplicate WIRE |
| SOF target and exit | Selected target changes only displayed send faders; MAIN/Channel fader and pinned MAIN never change; original GAIN/FX view returns |
| Hardware and persistence | Real ChannelId/EdgeId readback, .smix restore, restart/reconnect, native PCM and VST3 effects in both routes, explicit device epoch, ASIO primary isolation |
| GUI and performance | Single assembled \`/preview/\`; existing approved PNG/hardware faders unchanged; widths 1366, 1680, 1920 + applicable narrow; no lost focus, excess DOM rebuild, per-frame UI churn, realtime allocation or extra callback |

**Evidence levels:** Browser/UI smoke proves presentation only; native deterministic PCM tests prove signal routing; native exact-SHA package verifies build; genuine ASIO device/operator FAT proves installed audio behavior. Never elevate one tier to another.

## 7. Status ledger / next-thread handoff (as checked 2026-11-13)

- Current main observed: \`6309d79b5ff8e873e64326f5319c177aba118235\` (refresh; not permanent authority).
- [PR #604](https://github.com/masarray/arsonmix/pull/604): open draft GAIN/FX interlock and contextual SOF browser reference, guards against −60 dB WIRE deletion and ambiguous bulk PRE changes. Do **not** claim native audio shipped.
- [PR #606](https://github.com/masarray/arsonmix/pull/606): open draft, more advanced integrated source base **including #604** and work on 8D-SonKu. Never independently merge both without reconciling duplicate changes. 8D-SonKu and Harmonice are separate plugins from this mixer feature.
- Native digital gain, a proper **PRE-FADER after FX** tap, SOF enabled state, native per-EdgeId level/tap/ON ACK/readback and physical ASIO proof remain **implementation work**, not documentation-complete.
- Before any code changes: refresh main, PR #604/#606 heads, actual source/CI/merged status. Implement one bounded integrated vertical milestone in the live advanced branch or a correctly rebased successor. Preserve the SonKu/editor changes; do not overwrite someone else's branch head. Follow [ARD](ARD_CONTEXTUAL_CONSOLE_GAIN_SOF_20261113.md), root [AGENTS.md](../AGENTS.md), and the [fast feature playbook](architecture/FEATURE_EXTENSION_PLAYBOOK.md). For docs-only updates no expensive Windows build is needed.

## 8. References (patterns, not visual/DSP copies)

- Waves eMotion LV1 Classic user guide: https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html
- Yamaha DM7 reference/StageMix manuals (FADER/SENDS mode, pre and on workflow): https://usa.yamaha.com/products/proaudio/mixers/dm7/downloads.html
- Allen & Heath Avantis user guides: https://www.allen-heath.com/hardware/avantis/avantis/resources/
- Studio One / Fender Studio Pro Console/send workflow: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm
