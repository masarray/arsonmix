> **2026-11-04 — USER'S FINAL GUI AUTHORITY, OVERRIDES ANY CONFLICT BELOW.** Product GUI/UX is **https://masarray.github.io/arsonmix/preview-v2/** and the latest qualified `main/site/preview-v2/index.html` + `site/preview-v2/assets/**`. The native engine MUST be stitched beneath that already-approved renderer, not the other way around. Never implement or ship alternative UI `web/index.html`, old M4/`web/mixer-layout.js`, ENGINE `web/console-native.html/.css/.js`, frozen PR #278/M3 design, or the `/preview/` PNG-button variant as product entrypoint. Deterministic bundle + asset provenance + only thin native adapter, preserving current WIRE/fader/meter/FX/PAN/MAIN controls and density. Native accepted intent→ACK→same-revision snapshot, one JUCE PCM graph/callback/VST3 owner, true device pins and real-time safety still required. GUI P0 [#389](https://github.com/masarray/arsonmix/issues/389), ENGINE #266/#387, FAT #270. [Current handoff](workstreams/HANDOFF_APPROVED_PREVIEW_V2_NATIVE_20261104.md). Older references to #271 GUI implementation or PR #278 as active design authority are **SUPERSEDED, not release instructions**. Until a real Windows EXE embeds this GUI and passes visual/PCM/VST3 FAT, **NOT PRODUCT READY**.

> **ACCEPTED GUI DISPLAY OVERRIDE · V59 merged #383 (2026-11-04):** See [main→native reconciliation](workstreams/WIRE_V59_MAIN_NATIVE_ENGINE_RECONCILIATION_20261104.md). The canonical CH→MAIN **edge still exists for each connected channel**, but its visible fragments appear only for **one selected channel in expanded WIRE**. Collapsed WIRE shows **no** physical MAIN tails; OUT color means non-MAIN custom sends. This supersedes earlier paragraphs suggesting every unselected channel continuously paints a MAIN cable. Native PCM remains unqualified.

# ArSonMix PRD — WIRE-First Live Routing (2026-11-04)

**Status:** APPROVED USER PRODUCT DIRECTION / SPECIFICATION, NOT IMPLEMENTATION OR AUDIO QUALIFICATION. **Target release:** first integrated, reliable Windows live mixer. **Priority:** overrides older PRD v5.2 clauses prescribing dedicated Group Bus / FX Return creation in the *first* release, without deleting existing native-compatible AUX/Show/FX code or relaxing safety. **Companions:** [ARD WIRE First](ARD_WIRE_FIRST_ZERO_ADDED_LATENCY_20261104.md), [owner execution handoff](workstreams/HANDOFF_WIRE_FIRST_GUI_ENGINE_FAT_20261104.md). Existing [PRD v5.2](PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md) and [ARD v5.2](ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md) remain in force where not superseded.

## 1. Customer intent and release scope
ArSonMix shall feel like the approved beautiful, compact, high-quality hardware-style live Console (PR #278, #351 and approved V51 meter/V52 fader checkpoints), perform low-latency native ASIO/WASAPI mixing with real VST3, and expose FL Studio-like arbitrary legal internal routing through physical-feeling WIRE jacks. **No fresh UI redesign, text-based routing badges/cards/CTA, DAW timeline, second audio engine, or special-purpose BUS/FX routing system.**

**Now:** universal audio channel, direct verified physical input, mono/stereo processing, native inserts (EQ/reverb/compressor etc.), fader/pan/mute/solo/PFL when truly supported, OUT jack, IN jack, multiple fan-in/fan-out WIRE edges, pinned MAIN, live meters, Matrix/Navigator for advanced/accessible operations, safe Show/session persistence and observed device health.

**Next development, not current release blockers:** dedicated +BUS/Group Bus wizard, dedicated +FX RETURN workflow, new role-specific routing subsystems, advanced sidechain graph, DCA/VCA, optional experimental free-feedback routing. Existing legacy AUX, Group and FX Return code/state are **preserved for compatibility and migration**, but new first-release creation entrypoints are feature-flagged off until complete. An ordinary universal channel can sum multiple incoming audio edges, contain VST inserts and work as a submix or wet return. A user can choose a channel named Reverb or Vocals; role is NOT a new graph.

**Product truth:** zero *added samples* for pure internal graph routing without latency-producing processors, as measured by deterministic PCM tests; not zero device/driver/converter roundtrip milliseconds. Performance at least competitive with LV1-class workflow on comparable supported hardware; claims of being lighter/better require side-by-side comparable measurements. No guaranteed xrun-free on arbitrary third-party VST3/host hardware.

## 2. Reference behaviors (follow evidence, not copied intellectual property)
Image-Line:
- Mixer Track routes to one or multiple other Mixer Tracks. A new send retains source-to-Master by default.
- Send is post-fader by default. PRE-fader/pre-FX needs an explicitly defined tap/processing model; do not silently reinterpret.
- 'Route selected to this track only' creates the route and disconnects Master in one logical operation.
- Clicking Master send allows disconnect. Feedback-producing options are disabled.
- The FL Studio manual specifically describes a cable to Master visually dropping off the lower mixer edge rather than spanning the entire screen. ArSonMix takes this ergonomic pattern further with a paired MAIN-colored under-chassis cable.

Waves eMotion LV1 Classic: Latency Optimized processes the signal path in one processing cycle with no internal routing latency. DSP Optimized may improve efficiency with latency trade-offs. Prefer a single-cycle low-latency live graph; compare DSP headroom and latency *with explicit buffer hardware conditions*.

Official references:
- https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm
- https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm
- https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm
- https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_trackprops.htm
- https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html

## 3. Visual and interaction contract — no routing explanations disguised as new UI
**Preserve** the selected machined graphite/chassis visual, genuine existing 3D/recessed jacks, meter/fader geometry, icon/color/identity, FX Rack, compact rail and pinned equal-width MAIN. Routing identity is conveyed by *physical geometry*, material and cable color, not by new text labels, cards, arrows, buttons, tooltips floating permanently or badges.

**Paired MAIN cable design (V59 accepted):** only a uniquely selected channel with an active direct-to-MAIN edge displays a short cable colored from the existing MAIN channel palette emerging from its OUT jack, hanging downward into the hidden chassis. The pinned MAIN has a matching short cable emerging upward from below into its IN socket. Both fragments represent **the same logical edge** (not two sends or extra DSP), and no full-width MAIN cable spans channels. Short hanging curves must have restrained realistic wire sheath, smooth tangent/weight, subtle shading and precise plug/metal tip geometry; no neon/glow/filter/everlasting bounce. Existing channel-to-channel WIRE remains source-channel colored. Nothing encroaches on PAN/MUTE/fader or expands strip height.

**Focused MAIN edge semantics:** MAIN receives many channel feeds; its single visible physical jack is a context-sensitive inspector for the specifically focused/selected source edge. Selecting CH7 must make MAIN jack operate on CH7->MAIN, not on all feeds. Without a uniquely identified source, clicking/dragging MAIN must NOT disconnect all channels: offer existing unobtrusive route selection through Navigator/Matrix or require selecting one channel. The unselected aggregated state may remain physically plugged if >=1 MAIN feed exists; do not imply a specific source without selection.

**Physical gestures:**
- New audio channel: direct MAIN audio edge ON by default; the selected channel shows both cable fragments only when WIRE is expanded. Unselected or collapsed cables are hidden without changing native routing.
- Select channel: see its direct MAIN cable state, plus all selected-source WIRE paths; off-bank route remains valid.
- Drag plug out of MAIN IN: thresholded drag candidate, live moving plug physics only; release in intended empty WIRE bay disconnects **only focused source->MAIN**. No accidental global mute. Escape, pointercancel, invalid drop or unchanged selection restore original exact graph + tip position. Reconnect by OUT->MAIN IN snap. Drag back onto origin cancels disconnect. Keyboard and touchscreen-safe alternative via pre-existing Matrix/Navigator.
- A short click on connected MAIN socket inspects/selects the edge; explicit click toggle can be supported when deliberate/not confused with drag. It must never silently toggle when the user intended drag.
- Drag CH1 OUT -> CH3 IN normally adds a parallel post-fader route and retains CH1->MAIN. Route-only is an explicit command/macro: atomic create CH1->CH3 and remove CH1->MAIN; other unrelated existing sends preserved.
- Disconnect CH1->MAIN while CH1->CH3->MAIN still exists: audio may continue through CH3; do not falsely report silence. The WIRE graph, not last edited control, determines audible reachability.
- Selected cable maps to exact stable edge ID for gain, unplug, replug, Undo. Multiple edges arriving at one jack never imply 'disconnect all'. Replug retains gain/tap when valid.
- Viewing/banking/filtering/collapsing/reducing motion does not change any routing edge or audio. In collapsed WIRE space show only flat IN/OUT dots for committed custom routes, **no physical MAIN stub/cable**; no resizing approved meter/fader/chassis.
- WIRE displayed colors and connections derive from accepted graph / GUI candidate state only, not CSS guesses or hard-coded CH indices.

## 4. Single connected control surface
WIRE jack, visible cable, channel OUT selector, MAIN socket, WIRE Navigator and Matrix, WIRE gain, Sends-on-Faders if retained as a view, Scenes/Undo all operate on exactly one canonical channel/edge graph. INPUT physical patch is different from internal channel audio routing and cannot be conflated with WIRE receiving socket. Plugin Insert FX is within a channel, not a separate routing network.

The legacy reference fields 'wireEdges', 'track.output', 'sendSlots', 'track.input' can be imported/migrated but must not independently own accepted routing after conversion. Existing inbound sends, primary output, PRE-FX split and MAIN must be visible or introspectable in the same graph; never create an invisible second feed.

Aesthetic feedback uses a lit physical jack, plug occupation, cable movement/selection color, and subtle inactive finish. Error detail may appear transiently in the **already existing** console status mechanism or accessible semantic status, not new badges/cards/overlays.

## 5. Context-aware behavior and safety
Smart Routing Guardian makes deterministic decisions using current graph, source, destination, tap, selected edge, device epoch and user action. It shall NOT use AI inference, noisy periodic graph polling, hidden reconfiguration, automatic wet/dry plugin control, or uncontrolled worker thread creation.

- ALLOW: legal fan-in, fan-out, chained universal channels, quiet source-only nodes, independently named reverb/monitor channel.
- WARN without forced changes: reconvergent dry paths / potential doubled dry (+6.02 dB for two identical 0 dB paths), high plugin latency, mute PRE-fader semantics, orphan output/reachability.
- BLOCK: cycle from *any* edge type, self route, invalid or inactive ID, stale epoch/revision, incompatible format/resource budget, duplicate enabled audible edge with same semantics, unsupported live operation, unsafe output ownership.
- If an existing route is under control, 'already connected' means inspect not toggle-away on casual drop.
- Emergency output mute never waits for Guardian/UI; guard external virtual-cable/physical/acoustic feedback separately because internal DAG cannot see all external loops.

**MVP edge policy:** one enabled audible edge per source/destination pair, including MAIN, with canonical POST_FADER tap and independent gain. This avoids ambiguous multi-tap duplication. Future multi-edge per pair or sidechain requires explicit identity, distinct send semantics, UI affordance and audio tests; do not silently merge distinct taps by pair in future.

## 6. Definition of Done (all mandatory, no partial release claims)
1. On GUI reference, CH1..CHN initially show MAIN colored cable stubs and exactly one corresponding logical MAIN edge each; no phantom duplicate sums.
2. MAIN plug is draggable from its pinned IN, can disconnect/reconnect exactly the selected CH's edge; drag cancel and invalid drop 100% restore; no global MAIN mute.
3. CH->CH sends, Master send, matrix, OUT, Undo, Scenes and WIRE visual produce identical graph snapshots after all legal gesture sequences. Invalid graph changes never persist.
4. Cycle and duplicate detection pass all multi-step adversarial sequences: WIRE+legacy SEND, WIRE+OUT, WIRE+PRE-FX split, Scene+SAFE, Copy/Paste, cross-bank and late replug. No invisible legacy send.
5. Added dedicated BUS and FX Return commands are absent/disabled via feature flag on the WIRE-first release UI, *without deleting existing native AUX/Show sessions*. FX inserts remain valid.
6. True native input->channel->MAIN PCM, 2 independent source channels, CH->CH->MAIN, parallel dry/wet, 3-hop chains, multiple fan-in/fan-out, unplug/reconnect, RT mute/fader, 2 independent native VST3, Show roundtrip proven on one callback and one accepted graph; dynamic CH11 when claiming dynamic channels.
7. Pure-routed graph adds **0 samples** beyond source->sink transport under same callback, measured by impulse sample index; plugin latency/PDC reported honestly and resolved or warned on reconvergence.
8. 48 kHz test profiles 64/128/256 frames when actually supported; 16/32/64 channels as admitted by memory/callback budget; 0/medium/high FX cases. Publish callback p50/p95/p99/max vs deadline, xruns, PCM path latency, memory, UI frame work and precise device conditions; do not certify 'lighter than LV1' absent comparable same workload hardware.
9. No audio callback allocation, locks, UI, plugin scan, disk, graph rebuild, driver enumeration, expensive vector traversal or JS roundtrip. No new crackling; 0-FX physical incident #15 remains stop-ship until reproduced, root-caused and retested.
10. Exact integrated SHA same producer/GUI/validator, Windows package + Chromium functional/visual parity + actual/native device evidence. Screenshot or GUI demo PASS alone is NOT release.

## 7. Priorities and compatibility
This PRD overrides **first-release feature ordering**, NOT the v8 schema, native lease ownership or any user-approved console geometry. Continue existing PR #266 ENGINE, #389 native GUI, #270 FAT, #352/GUI #351 visual and #360 fader checkpoint with clear file ownership. Dedicated BUS/FX returns remain preserved capabilities and next-development work; don't delete DSP code or unknowingly break saved shows.

Read [handoff](workstreams/HANDOFF_WIRE_FIRST_GUI_ENGINE_FAT_20261104.md) for exact files, owners, milestones, tests, and restart procedure.
