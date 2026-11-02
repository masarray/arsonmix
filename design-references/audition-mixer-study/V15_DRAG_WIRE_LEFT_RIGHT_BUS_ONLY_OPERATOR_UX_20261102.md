# ArSonMix V15 — Physical Drag-Wire Patch Bay and Easy Bus-Only Routing

Date: 2026-11-02 | Source of truth: PR #278 isolated reference | **GUI DEMO ONLY**, not native PCM and no accepted audio graph ACK.

## Official DAW workflow research

1. [FL Studio mixer routing](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm): A source can send to selected tracks, or **Route this track to selected only** which also deselects direct Master send. It can create a submix, selected tracks to this track only, ordinary sends and sidechain routing. Track order is not a signal-direction rule. It supports a dedicated “Routing cables” display.
2. [Fender Studio Pro / Studio One Effects Signal Routing](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm): primary channel OUT to Bus is serial group routing; **Sends** are parallel and independent of primary OUT. FX Returns own independent insert chains/fader/OUT and can send onward; drag an effect onto Send creates a new FX Return and a Send.
3. [Adobe Audition multitrack routing](https://helpx.adobe.com/audition/desktop/mixing-multitrack-sessions/multitrack-routing-eq-controls.html): Master/Mix is a terminal output to hardware; Bus output can reach another Bus or Mix; multiple Sends to different buses are independent of primary Output. Bus chaining and monitor mixes are genuine, not alternate labels for a fader LINK group.

**UX decision**: borrow FL Studio's direct source-first interaction, Studio One's serial-vs-parallel semantics, Audition's explicit primary output separation. Express it with original ArSonMix physical patch jacks (not imported FL graphics): **OUT ● (filled) → IN ○ (hollow)**, a cable that follows the pointer, and a safe nonmodal decision for a Bus.

## Implemented on PR #278

- Every ordinary audio channel, Group Bus, and FX Return has its own **filled SEND OUT** node. Every other channel has a **hollow SEND IN** socket, and Master is the terminal (no OUT). The source is not inferred from left-to-right order.
- Drag filled OUT → hollow IN; start dragging after a 5px displacement threshold. Lightweight SVG cable follows the actual pointer; release over an accepted input socket commits via the SAME `requestWireConnection()` guard used by click-to-click routing. Canceled/off-target drags leave the audio graph unchanged.
- Drag from CH08 RIGHT to CH03 LEFT is valid. Source socket clicks also arm that source without requiring the user to start dragging. Click target IN after switching to another bank is a low-error alternative for long-distance links; no invisible auto-scroll/teleport.
- Routing labels remain tiny, with separate engraved OUT/IN, connected/inactive sockets, and one per-link gain dial. Selected source is tinted green; connected send shows green/cyan depending on declared tap; pending hover line amber. The socket's visual result is browser state only, not a genuine live PCM ACK.
- Compact wire view retains connection cables; expanded wire view provides taller, recessed routing canvas. The P0 fader/PAN block must remain at the approved stationary position; FX rack absorbs ROUTE/WIRE fold changes.
- All source roles except terminal Master may originate a wire (Audio/Bus/FX). Group Bus and FX Return have their own valid OUT path to Main or another legal bus.

### Dragging to a Bus: exactly two nonmodal choices

- **VIA BUS ONLY** (recommended serial path): one primary route **CH → BUS → MAIN**. Source primary OUT becomes the Bus (and therefore is **not** direct MAIN). A `kind:'primary',mode:'routeOnly'` reference edge visualizes the one serial primary route, not a parallel audio Send. Disconnecting it leaves source `SOURCE ONLY (No Main)` rather than silently restoring an additional direct-main path.
- **PARALLEL SEND**: one dedicated `kind:'send',mode:'send'` edge is created; current primary OUT is unchanged (typically direct MAIN). Good for shared Reverb, FX processing, foldback and layered send mixes. The operator must choose; merely touching a Bus jack must not mutate Main feed.
- **Master as destination**: a direct Master jack toggles primary Main only when the source currently routes to Main or Source Only. When primary route is through a Bus, direct Master cannot be silently re-enabled; changing to Main must be an explicit higher-level re-route.
- **Legacy Send conflict**: if an existing hidden legacy Send connects this source to the Bus, do not create a second audible path or silently delete the old send. Refuse with an explanatory message until the canonical graph migration is accepted.

## Workflow and failure-mode acceptance matrix

| Case | Expected soundman workflow | Native audio invariant |
| --- | --- | --- |
| CH1→CH3, rightward | OUT1 drag → IN3, or select CH1 then click IN3 | One read-only source buffer, one valid accepted edge |
| CH8→CH3, **leftward** | OUT8 drag LEFT → IN3 | Destination location never limits graph direction |
| CH1→CH3/4/5/6 | Click CH1 → CH3, Shift-click CH6; or 4 successive drags | Independent per-destination processing and gain |
| CH1→Bus A→Master **without direct Main** | Drag OUT1 → IN Bus A → **VIA BUS ONLY** | Exclusive primary Out=Bus A; Bus A Out=Main once |
| CH1→Bus A + Main simultaneously | Same gesture → **PARALLEL SEND** | Keep Main, create one extra send; warn about gain/headroom |
| CH1→FX Return Reverb | OUT1 → FX Return IN | Parallel wet/send, preserve dry Main; dedicated return VST insert |
| Bus A→Bus B→Main | Bus A OUT → Bus B IN, choose **VIA BUS ONLY** | Acyclic graph, single serial primary route |
| FX Return→Bus A→Main | FX OUT → Bus IN then choose route-only | Independent FX return insert/fader and valid downstream |
| Bus A→CH3 while CH3→Bus A | Attempt reverse drag | Reject transitive cycle; preserve previous accepted graph |
| OUT1 released on blank metal | pointer cancel/no valid input | No edge created, no source/fader moved |
| Source and destination same strip | OUT1 → IN1 | Reject; no feedback/self-loop |
| Source or target hidden in next bank | Click source, switch bank, click target IN | Source stable across navigation; off-bank edge persists |
| Channel renamed / restyled | Rename source/Bus | Stable IDs, no broken connections based on mutable text |
| Main and secondary WASAPI monitor | Source to Main, monitor sink mirrored best-effort | No secondary sink stalls clock or ASIO audio callback |
| Device disconnected / reopened | Mic ASIO deviceEpoch changes | Existing accepted graph never points at stale hardware pins |
| Scene recall and safety | Restore prior source/Bus paths | Single atomic native graph revision or reject, no partial states |
| Mono Mic to stereo return | Select/route legal width | Explicit channel width and legal L/R projection |
| 16+ sends / high CPU | Many destinations | No per-route physical capture duplicates, bounded RT work |
| Sidechain-only | Advanced destination route type | No audible duplicate dry signal; not falsely drawn as normal send |
| Physical output routing | Bus/MAIN OUT to device pins | Separate native hardware patch, never mistaken for channel-to-channel wire |
| Dragging over input with legacy Send | Refuse duplicate; show migration reason | No two audible unintentional paths to same destination |
| Soft keyboard/screen-reader use | Choose Source OUT; click IN; adjust native range | Controls have unique labels and focus ring; not drag-only |

## Remaining production gates

1. Existing `V14` upper IN/OUT UX audit remains **unimplemented** here: verified audio PortRefs, primary OUT summary, canonical unified graph and real Send-on-Faders must be done by native/UI ownership. Current V15 browser demo still has upper IN/OUT dropdowns.
2. New V15 pointer gesture needs actual Chromium drag screenshot/geometry acceptance at 1366×768, 1680×900 and 1920×1080, all three densities; CSS should be reviewed manually for 28–30px nodes, cable contrast and green/amber semantics. A JS DOM state test does not prove visual hit testing, `elementFromPoint`, nor tactile aesthetics.
3. Native graph: prepare structural changes off callback, stable ChannelIds, target width, loop/duplicate-PATH preflight, `requestId/baseGraphRevision/deviceEpoch` and accepted snapshot ACK, sample-accurate PCM fanout and source tap semantics (POST FADER vs PRE INSERT), PDC/latency/headroom and 30-minute real ASIO FAT. No GUI green “LIVE” until native graph ACK.
4. Future ergonomic enhancement: add a compact post-drag **Undo route**, safe Replace/Swap when an existing primary route is present, advanced SIDECHAIN-only and destination navigator for off-bank source/target.

## Source boundary

All implementation is isolated to `design-references/audition-mixer-study/index.html` and companion smoke/QA files within PR #278. The source remains Draft. Do not merge the browser demo into production engine as if its wires were real PCM.
