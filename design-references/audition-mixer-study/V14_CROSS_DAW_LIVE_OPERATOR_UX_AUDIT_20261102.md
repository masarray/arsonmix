# ArSonMix V14 — Cross-DAW Workflow Audit and Live-Sound Operator UX Decision

Date: 2026-11-02 | Target: owner-selected GUI reference PR #278, **V13.2** | Status: AUDIT/PROPOSED UX CONTRACT, **NOT** implemented native audio
Scope: learn from official **FL Studio**, **Fender Studio Pro / Studio One lineage**, **Adobe Audition**, plus **Waves eMotion LV1** live-console patches where applicable. Keep ArSonMix an **operator-first live digital mixer**, NOT a DAW timeline.

## 1. Reliable reference facts — keep semantics separate

1. **FL Studio Mixer** uses external hardware IN/OUT selectors *separately* from INTERNAL insert-to-insert sends. The operator selects the **source mixer track**, clicks a **destination Send switch**, and adjusts the **send-level knob** on that target. Existing source→Master route remains unless explicitly disabled. FL supports multirouting and prevents feedback cycles. An insert may receive both physical input and internal sends. [Mixer audio routing](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm); [Mixer explained](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm).
2. **Fender Studio Pro (successor lineage of Studio One)** exposes compact Input and Output labels at channel top, shows input-type/Output/FX/Bus distinctions, supports click-to-assign or ordered multi-assignment, and hides/shows detailed Sends/I/O. Bus/FX channels list their contributing tracks rather than pretending to have a single hardware input. Main Output remains pinned to the far right. Its Send rack can create a new FX Return by drag/dropping effects; this is excellent for beginners. [Console](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm); [Effects signal routing](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm); [Hardware I/O matrix](https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm).
3. **Adobe Audition** groups Input/Output, FX, Sends, and EQ into independently visible/collapsible sets. Its Output chooser distinguishes Bus/Mix/hardware. [Multitrack routing](https://helpx.adobe.com/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html).
4. **Waves eMotion LV1** separates external I/O device patching, internal routing to groups/MAIN/matrix, and processing; output tap choices include INP, PRE, PST and PSP. This is particularly relevant to live sound but should not force ordinary operators into a matrix. [Patch tutorial](https://www.waves.com/emotion-lv1-tutorial-2-2-using-the-patch-window); [Mixer channel strip](https://www.waves.com/emotion-lv1-tutorial-4-5-mixer-channel-strip-1).

These are documented application capabilities, NOT independent empirical usability scores.

## 2. Current V13.2 source audit — evidence in index.html

| Priority | Current observable implementation | UX failure / exact risk | Decision |
|---|---|---|---|
| P0 | `channelOptions(...,'input')` combines hardware, CABLE L/R, and `CH## PRE/POST-FX SPLIT` in **one** IN picker | Three different routing concepts are one menu; conflicts with new direct wire target selection | **IN is external source binding only** on main strip; Move internal split to WIRE edge/tap inspector. Underlying migration must be versioned |
| P0 | `channelOptions(...,'output')` exposes Master, Bus and hardware together; pinned MAIN socket and WIRE also route | Duplicate OUT authorities; selected source may have implicit main dry + send, wrong perception of routing | Keep compact **OUT summary/status** but no duplicate inline destination dropdown. Primary MAIN/GROUP direct route controlled by WIRE (unique graph authority); physical endpoints via I/O Patch drawer |
| P0 | `init()` starts with hidden active `Host → Bus A` and `Interview → Bus A` legacy sends | After V13 removed visible legacy Send picker, the **live route can be invisible** from wire; violates soundman trust | Unify/visible-project all *accepted* Send/primary edges to WIRE; migration must de-duplicate safely and distinguish LEGACY until reconciled |
| P0 | `sofTarget`, `getSofLevel`, `setSofLevel` still address legacy `sendSlots`; `wireEdges[]` is separate | Sends-on-Faders can change the **wrong gain** or do nothing to an active wire | SOF uses same canonical stable-ID edge gain as WIRE. Disable/defer unsafe SOF until native confirmed; show strong mode indicator |
| P0 | `wireConnect` restricts source to `kind==='audio'`; `wireSourceId` only changes for audio; `wireFor` excludes Main as standard edge | Bus/FX cannot send onward as FL Studio does, and MAIN state is not modeled uniformly | Support legal source roles (Audio, Bus, FX Return, MON if allowed) with graph cycle/role validation. Main primary/output remains terminal, not send source |
| P1 | `wireMode` global select toggles only **new** edges; current wire mode varies per edge | User assumes changing dropdown modifies currently selected link; existing sends silently keep prior tap | Default edge type inferred by destination role; active edge popover shows tap/level/edit + exact status; never global ambiguous tap mode |
| P1 | `renderWireRouting()` clears cables if selected source is off-bank | Operator sees no wires when bank changes despite accepted links | Docked SOURCE indicator plus on-bank inbound/outbound count; “Show receivers”/“Jump to source” navigation, no fake off-bank cable |
| P1 | Selected channel for gain/FX and selected **wire source** share channel-click state; modifying OUT ends with `selectTrack(t)` | Editing a receiver can silently change the armed wire source | Split explicit **inspectedChannelId** from **wireSourceId**; no silent source turnover during editing, use color-coded SOURCE lock |
| P1 | Top IN can be `None` while meters can animate in demo; input choices are hardcoded device names | Missing “No input / disconnected / device unavailable / pending / accepted” truth; real audio could be misunderstood | Native verified PortRef, deviceEpoch, mono/stereo pins and input level; never claim audio flow from UI-only level animation |
| P1 | Master primary OUT and direct destinations can be set in both ROUTE and WIRE, with role-named options | MAIN ON/OFF becomes ambiguous if direct OUT goes to Bus or hardware | Distinguish primary mix destination from hardware endpoint. MAIN switch reflects exactly accepted direct Main edge, never guessed from a string |
| P2 | FX rack has quick add, but no drop-to-wire FX Return action | Creating shared Reverb requires manual FX Return + plugin + routing | Drag effect onto **WIRE ➕ FX RETURN** to create and patch one wet return automatically |
| P2 | Section rail has primarily cryptic glyphs and both “Narrow” and “View Compact” | Mode/UI terminology duplicates; first-time users must decode icons | Compact text/tooltip: INPUT, INSERT, WIRE, PAN, LEVEL; unify density controls, keep shortcut names and contextual help |
| P2 | “Group” button actually links faders, while GROUP Bus is a different DSP role | An ordinary user might expect a combined Drum Group | Rename **LINK FADERS**, separate **ROUTE TO BUS** (exclusive serial audio submix), and **VCA** future-only |

## 3. Recommended single-console hierarchy (wire-centric)

```text
[CH01 · Vocal Mic]                         Selected SOURCE chip: 01 → 4 targets
[IN  🎙 Mic 1 • ASIO • Mono • OK]             [Ø] [M/ST] [ⓘ] 
[FX  EQ → COMP → +FX]                      (rack elastic when ROUTE/WIRE folded)
[WIRE     source ⇣══▶ CH3,4,5,6]          80px compact / 160px expanded
[OUT  ● MAIN  •  ⇢ 4]                     optional ONE compact summary chip;
                                          no second always-open dropdown
[PAN  Center]                              fixed Y position
[MUTE | SOLO | PFL]                        fixed Y position
[LED VU  /  FADER]                         fixed height and Y
[CH01 Vocal Mic]                           name/identity
```

**Top IN:** always-visible **one source binding summary**, click to open an anchored nonmodal **INPUT PATCH** popover. Source groups: Hardware ASIO (named verified mono/stereo pins), Virtual Input (verified bridge/device source), Channel Copy/MULT (advanced; do not mix with hardware list), None. Include current device, port availability, last accepted source, channel processing width, estimated latency status. “Unsupported/disconnected” fail closed; do not offer hard-coded dead endpoints.

**Top OUT:** Retain **one glanceable destination/status**, not a second competing routing authority. Default `● MAIN`; when routed exclusively to Group show `● BUS DRUMS → MAIN`; when multiple direct sends show `⇢4` WIRE. Click OUT for read-only route inspector and an **Advanced Hardware Output** patch action, not a full duplicated destination dropdown. Move physical output mapping (Main, Monitor, broadcast return, direct outs) to named output buses in a compact Audio I/O drawer; advanced matrix available on demand. This refines the old v5.2 PRD requirement “Direct compact IN and OUT” instead of deleting visibility entirely.

**Central WIRE:** uniquely owns INTERNAL routing. Clicking CH1 source then CH3+Shift CH6 creates 4 links; per-edge hardware knob; accepted direction/role/tap labels. Hover preview amber dashed, linked green/cyan when ACK accepted. Shift-range and unlink remain. Context menu **Send** vs **Route Only** vs **Sidechain** where appropriate. Provide destination-group role defaults with explicit undo, never silently delete a primary direct-Main edge.

**Rule per target role:** Audio channel → ordinary POST-FADER send (or advanced PRE-INSERT split); FX Return → parallel Send; MON/AUX → independent monitor send default PRE-FADER, explicitly configurable; Group Bus → operator chooses “ROUTE ONLY” (serial group, remove direct MAIN) versus “SEND PARALLEL”; Main → direct output route. Both Main and external hardware output are distinct concepts.

**Processing vs engineering:** Always-visible mic source, compact IN health, FX bypass, Pan, M/S/PFL, fader, VU, selected route count. Higher-detail gain/polarity/device matrix, plugin latency, tap, snapshot transaction in popover or nonmodal inspector. Avoid separate DAW timeline, full-screen patching or multiple redundant modes.

## 4. Five primary soundman workflows (target interactions)

1. **Plug in microphone → sound:** select active device once; CH1 IN → Mic 1, observe RAW input meter and Output/Main state; adjust CH1 fader, MAIN meter follows. No need to create a Wire to hear basic channel.
2. **One source → four channels:** click CH1; click CH3, Shift-click CH6. Four independently adjustable send pots, source badges +4, receiver IN source hints. For independent FX processing of identical capture use explicit PRE-INSERT MULT mode/tap; avoid default source fader linkage confusion.
3. **Vocal → reverb:** drag Reverb plugin to `WIRE ➕ FX RETURN`, creates FX Return channel and sends CH1 to it with wet processor. Original dry to MAIN preserved; Send knob changes only wet amount. No manual Bus creation.
4. **Drums → one group:** multi-select audio channels, click `ROUTE TO BUS` → Drum Bus or +NEW. Source primary outputs replace MAIN, Group Bus routes once to MAIN. No accidental extra MAIN feed; CH buses remain separate from LINK FADERS.
5. **Stage monitor:** select MON bus → `SENDS ON FADERS`; source channel faders temporarily control accepted MON sends, not Main gains. Visible bright but restrained MON mode banner, safe return to MIX. Independent ASIO main and secondary best-effort mirror never share a realtime clock.

## 5. Phased owner decision and acceptance gates

### P0 — eliminate dangerous competing authorities
- Unify canonical signal graph in engine and UI: physical PortRef input, primary serial GROUP/MAIN route, parallel SEND, independent PRE-INSERT MULT, true hardware output patch. Every audible accepted legacy send must appear in WIRE or migration preview.
- Bind sends-on-faders, snapshot, wire gain and bus routing to the same accepted graph revision; make invisible routes and old slot duplication impossible.
- Keep approved V13.2 physical proportions: elastic FX; **PAN, MUTE/SOLO/PFL, FADER and footer stationary** across all ROUTE/FX/WIRE folds. Do not replace fader artwork or change app into multitrack DAW.

### P1 — UX simplification
- Top IN becomes compact device-bound source badge and popover; OUT becomes route status & inspect, not second authority.
- Explicit role-aware routing context, source lock and off-bank navigation; source → Master default visible and editable.
- Rename Group to LINK FADERS; show Bus/FX/MON roles, display routing from any legal mix node.
- Input/source/edge statuses: AVAILABLE, UNPATCHED, PENDING, LIVE, FAILED, DISCONNECTED. No false green accepted or fake meters.

### P2 — one gesture smart assists
- Drop FX on patch bay creates FX Return+Send, multi-select→Group Bus, quick MON mixes, clear duplicate/feedback preview, undo last route with a single action.
- One rare **Advanced PATCH** matrix exposes all internal/hardware graph edges, not required for the 90% quick workflows.
- Keyboard/source shortcut and accessible arrow/knob labels; color is secondary to icon/text shape.

**Production acceptance:** Native ACK/GraphRevision/DeviceEpoch of each audio edge, real PCM before/after each processor, 1→4 split vs send audible semantics, independent send levels, cycle rejection, device hotplug/pending vs live state, source/direct hardware monitor, snapshot reset and rollback; one primary JUCE ASIO callback, zero RT allocations/locks, independent best-effort WASAPI mirror. Browser DOM/Chromium green passes remain GUI-only proof.

## 6. Existing PRD/ARD reconciliation

Current `docs/PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md` correctly requires direct compact IN/OUT and verified device endpoints, but its older `SENDS dropdown shows...` requirement conflicts with owner-approved V13 wire-only console. **Revise PRD with a clearly versioned V14 addendum**, not by silently deleting technical behavior: the *underlying send graph/tap/gain* remains, its primary operator UI becomes the **WIRE** arrow/knob. SENDS ON FADERS remains a valid monitor workflow only after bound to wire edges. ARD already forbids WebView2 as PCM authority and requires native ACK; preserve it.

## Summary

**Keep** tactile physical console + Source IN + FX rack + middle FL wire + fixed PAN/M/S/PFL/LED/fader + pinned MAIN. **Remove from main strip** duplicated old output-picker authority and internal split entries inside hardware IN menu. **Move** hardware patch and per-edge advanced tap/level to simple popovers. **Unify** every audible route and fader Send mode into one accepted graph with a truthful, readable live representation. Do not conflate UI-demo click correctness with native audio.
