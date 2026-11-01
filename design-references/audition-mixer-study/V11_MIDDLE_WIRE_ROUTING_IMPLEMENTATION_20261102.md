# ArSonMix V11 — Source-first Wire Routing: Mid-console Preview

Date: 2026-11-02 | Authority: operator-approved PR #278 | Status: **isolated GUI reference**, not native ASIO/VST3 routing

## Decision: wiring in the middle, not below the hardware fader

FL Studio's own manual documents bottom-of-mixer destination Send switches, cable graphics, and per-destination send-level knobs. This is good familiarity evidence, **not** a reason to put ArSonMix's routing below the operator-approved tactile fader. In ArSonMix, preserve the lowest hardware controls (pan, M/S/PFL, fader, LCD meter, nameplate) and place a persistent **WIRE row exactly between FX rack and PAN**, reusing the previously overcomplicated SEND/BUS section.

- Collapsed SEND/BUS → **53px wire row**, never hidden. The original SEND header remains a compact status strip; advanced BUS/AUX send selection expands separately to 129px.
- FX rack is elastic (V9): extra vertical room flows to FX slots when ROUTE/advanced SEND collapses. Fader height and meter ruler remain invariant while FX is expanded.
- Existing 16 Sends to Bus/FX Return, Sends-on-Faders and Master/primary OUT remain modeled. Do not confuse 1→4 independent audio-channel source splitting with four receives into one Bus, or overwrite real input assignments.
- One only full-window SVG overlay sits over the visible **source/target wire strip**; no physical wires over meter/gain controls, no canvas, no audio-thread rendering, no per-meter-frame geometry recomputation.
- Visual language: source has prominent down-arrow and green socket; destinations show gray up-arrow; connected destinations show green diamond/socket; selected source draws curved two-layer green cable paths to connected on-bank destinations. Maintain readable labels for blind/color-vision accessibility.
- If the source is not visible in the current bank, show the source identity in the global banner. **Off-bank cables are not drawn across nonexistent slots**; connected on-bank destinations retain their linked sockets and receive labels. Bank changes do not change the topology.
- Material inspiration: FL Studio's cable/arrow affordances; physical satin metal, segment-glass meter, fader PNG and calm palette remain ArSonMix style. Match operator recognition rather than stealing another application's logo/pixel assets.

## Implemented in PR #278

1. Select channel source by clicking its channel strip, e.g. CH1. Source ID stays stable while clicking a destination wire socket (prevent delegated mousedown from replacing selection with target).
2. Click CH3, CH4, CH5 and CH6 up-arrows in the center BUS/SEND wire row. Four **separate receiver-side** `CH01 PRE-FX SPLIT` source bindings are created; each receiver keeps independent FX/fader/pan/meter/primary output.
3. On-source fanout badge displays `⇢4`. Global banner reads source and receiver count. The live SVG overlay uses source/receiver actual rectangles, paints curved 2-layer lines and does not capture pointer events. No layout reads within meter animation or audio callback.
4. Clicking a connected target again disconnects that one edge; other receivers remain. A receiver with a real/non-None source is refused (fail closed) rather than silently replaced. Circular/missing-source graph requests are refused by `splitWouldCycle`.
5. Source output stays unchanged; an explicit `SOURCE ONLY (No Main)` output choice from V9 lets the operator prevent an extra dry Main feed. The prototype must never say the audio actually flows.

**Verified:** 172/172 deterministic DOM/source and input-model smoke checks (including four receiver click paths, individual unlink and occupied-input refusal). **Unverified:** final rendered Chromium layout and manual visual aesthetics. Do not merge this source into web/native without screenshot + native ACK regression gates.

## Native engine strategy

- Normalize wire `Link(sourceChannelId, targetChannelId, TapPoint, Enable)` as an **immutable audio graph intent**. Demo strings are transitional; production uses stable IDs, graph revision, device epoch and unique request IDs.
- Two explicit audio semantics: **MULT/PRE INSERT** duplicates one captured PCM source buffer at pre-insert tap into several independent channel processors; **SEND/POST FADER** passes source channel's output under per-destination send level, FL Studio style. Never use the same control for both without clear tap/mode labeling. PRE/POST send fader is not the same as PRE/POST source inserts.
- Multiple sink processing from one source is **one capture, multiple read-only fanout edges**. Check channel format, latency/PDC, gain/headroom, routing graph cycles, stereo pin identities and hardware device epoch off realtime thread; publish prepared graph atomically to callback. No locks, allocations, scanning or geometry work in ASIO callback.
- UI shows PENDING until native graph ACK, no green live cable until accepted. Failed update returns reason and preserves old graph audio; viewer may preview pending connection in a distinct dotted amber line, never counterfeit LIVE.
- User edits a wire at 1→N while audio is running; the graph updates at buffer boundary with ramp/dezipper to prevent pops, without restarting ASIO or forcing WASAPI on primary clock.
- Every destination owns independent meter and insert chain; block inadvertent double direct-MAIN and monitor feedback loops. Native measured acceptance on Windows must separately test one→four with receivers muted one by one and different FX chains.

## Formal UX tests before merge

- 1680x900 Comfort selected CH1, click CH3..CH6: four visible green links, no crossing outside the WIRE row or obscuring MUTE/fader/FX, bank unchanged. Compare with FL Studio's target-arrow pattern.
- 1366x768 / 1920x1080 in all density modes and with ROUTE/SEND collapsed and expanded; rail, channels, fixed Master aligned; FX receives reclaimed height instead of moving faders.
- Keyboard focus, up-arrow/button aria-pressed behavior, no color-only status; occupied source refusals readable.
- Persistent source across destination clicks and no wire state reset during plugin browser, bank navigation, scenes, rename or density changes. No hidden source link from off-bank mistaken for lost routing.
- Verify PRE INSERT default independent of source fader; use native execution for real acoustic proof, not simulated meter sine waves.

## Primary references

- [FL Studio Mixer Explained — routing iconography and Send switches](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm)
- [FL Studio Mixer Menu — Route this track to selected](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm)
- [FL Studio Mixer Audio Routing — source to destination and Master default](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm)

### Deliberate V11 limitations

The UI reference currently only routes **ordinary audio-channel source → ordinary audio-channel target** using the prior V9 PRE INSERT binding. AUX/FX Return/Master wire sockets remain disabled indicators in the middle row, while existing SEND/OUT controls still work elsewhere. Batch-Shift routing, live wire gain knobs, native graph ACK and signed-off rendered screenshots are next milestones; none are claimed delivered by V11.
