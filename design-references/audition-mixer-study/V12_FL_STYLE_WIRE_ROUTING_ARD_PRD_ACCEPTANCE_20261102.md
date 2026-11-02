# ArSonMix — V12 FL-style Wire Routing (ARD / PRD / UX Acceptance)

Date: 2026-11-02 | Product: ArSonMix single-window Live Digital Console | Visual-authority branch: PR #278
**Status: implemented as interactive GUI-only reference. Not an operational ASIO/VST3 source-to-sink audio graph.**

## Sources and design decisions

Image-Line's [Mixer Explained](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm) and [Mixer Audio Routing](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm) establish the exact operator expectations: select a source insert; destination up-arrows switch its sends; an enabled send has a per-link level control; green cable traces reveal the path; source's Master routing is independent; impossible feedback paths must be disabled. The official [Mixer Menu](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm) adds the one-to-many routing command and “only” variant which disconnects source direct Master.

**Ownership decision:** use FL's *interaction grammar and direction iconography* with an original ArSonMix silver/satin console skin. Do not import the brand's proprietary image/button assets. Unlike FL, route wires **in the existing middle BUS/SEND strip** below elastic FX rack and above PAN / MUTE / SOLO / PFL / stable fader / glass meter. User explicitly approved this location. Do not reintroduce DAW transport/timeline.

## Beginner operator journey (maximum six actions for CH1→CH3–6)

1. Select **CH1** by clicking any ordinary non-wire part of its channel. Its wire socket displays a green **▼ SOURCE**.
2. Scan the middle row: unused destinations display **△ WIRE**; connected ones show green **◆ LINKED** with a metal gain knob and unlink ×. The pinned Master shows **MAIN ON/OFF** on its own socket.
3. Click arrows at **CH3, CH4, CH5, CH6** OR click CH3 then **Shift-click CH6**. Both produce the same four links and a **⇢4** source count.
4. For levels, turn each individual wire pot (accessible range −60 to +6 dB; native keyboard arrows; wheel is ±1 dB; double-click resets to 0 dB). Moving one pot must never move another.
5. Use **MAIN ON/OFF** in the Master wire socket to remove/restore direct Main feed without affecting other links (duplicated compact toolbar control for when source is off-bank).
6. Choose **Follow Fader** (default, FL-style POST FADER audible send) or **Independent (Pre FX)** (multiplies a PRE INSERT source tap into independent receiver processors) **before creating new connections**. Existing links retain their tap, the control does not silently change existing graph edges.

**Mental model for users:** “green source down-arrow → target up-arrow → cable → individual knob.” Every wire is directional. A gray/empty arrow is unconnected, green solid line is active **in the GUI reference only**, dashed amber is a hover candidate and not yet accepted. Cyan is PRE FX tap. Never require creating four Bus channels for CH1→CH3–6.

## GUI reference shipped to PR #278

- Independent `wireEdges[]` persisted as `{id,sourceId,destId,mode,levelDb,enabled}` with immutable stable numeric channel IDs. A wire edge is NOT the receiver's `input` string and does not overwrite physical/virtual inputs; it also does not replace the older advanced `CH## PRE-FX SPLIT` input selection.
- Multiple sources may send to the same receiver; each wire pair is unique, independently disconnected and independently leveled.
- `wireConnectRange()` preflights all same-bank destinations for feedback before a batch transaction. Shift-click links all visible contiguous destinations without repeated rendering; no partial commit if any proposed edge creates a cycle.
- `wireIsCycle()` traverses wire sends, existing advanced split inputs, Bus OUT and Bus send connections. Default Main is a separate primary output, never a hidden second graph edge. No claim of full native graph admission.
- Renderer creates only the selected source's **on-bank** cable splines, with two fixed-stroke noninteractive SVG paths per link. Candidate routes are drawn amber/dashed on hover. No cable overlays the fader, meter or FX; pointer events bypass the SVG overlay. No animation, canvas, per-frame geometry pass or JS audio processor.
- Real controls use semantic BUTTON/aria-pressed for sockets and native RANGE for gain; status bar announces wire changes and explicit **GUI DEMO ONLY** notices. Keyboard arrow and screen-reader labels remain available.
- Source selection survives target socket, unlink and gain movement. Bank switching keeps off-bank edges and the global source/count indicator; local SVG never paints to nonexistent strips.
- Scene Save/Recall now includes wire graph, stable next edge ID, selected source, routing mode and range anchor. Existing V9 input, BUS, FX Return, M/S/PFL, Send-on-Faders, FX picker and source hardware art remain untouched.
- V9 FX is elastically taller when ROUTE or advanced SEND collapses; fader and calibrated meter heights remain stable when FX is expanded.

## AV / accessibility standards

- Routing row stays visible in collapsed advanced Send, with charcoal wells, satin socket rims, defined arrow anatomy, luminous green/cyan states and non-distracting cable glow. No giant cards, bold headings or consumer DAW timeline.
- Glass LCD metering remains fixed-band dBFS independent of fader dB. Cable rendering must never touch meter canvas or realtime scheduler.
- 1366×768 Comfort, 1680×900, 1920×1080; Standard and Compact too. No horizontal strip overflow; Master pinned and rail aligned; no FX/fader overlap. Screenshots require manual human signoff, not just static source checks.

## Engine implementation plan — DO NOT label LIVE until native ACK

**Wire protocol** (example):
```text
WireIntent {
 requestId, baseGraphRevision, deviceEpoch,
 sourceChannelId, destinationChannelId,
 kind: POST_FADER_SEND | PRE_INSERT_MULT,
 amountDb, enabled, mainFeedPolicy
}
WireAck { requestId, acceptedGraphRevision, deviceEpoch,
          acceptedEdges, rejectedEdges[reason], measuredLatencySamples }
```
- **Data plane:** a single captured PCM buffer fans out by read-only references. POST FADER uses source FX/pan/mute/fader result with its own send gain. PRE INSERT uses pre-FX input tap and receiver's independent FX/fader/pan/output. Clarify source mute/solo/tap policy before native delivery.
- **Graph:** validate source/target stable IDs, target role, no feedback cycles, no duplicate unintended direct-Main, existing physical input summing and format compatibility, plugin delay compensation and deviceEpoch; create graph off callback, publish at buffer boundary atomically, crossfade/ramp to avoid pops. No allocations, locks, VST scans, JS layout, or WASAPI clock sleeps on ASIO callback.
- **State truth:** pending preview is dashed amber, ACK-connected live edge is green; rejection displays cause and **preserves previous PCM graph**. Current browser reference uses green as *GUI state only*, with unambiguous DEMO status bar and no native audio claim.
- **Monitoring:** main ASIO monitor is the stable priority, secondary buffered WASAPI mirror cannot stall or synchronize the ASIO callback. Do not implement a second resampling mixer to fake a wire.

## Verification

**Completed:** 191/191 deterministic zero-dependency JavaScript/DOM workflow assertions (source-first one-to-many, multi-input retention, range linking, per-wire gain, mode, cycle checks, direct Master, scene recall, static material invariants). `visual_wire_v12_qa.py` is included for actual Chromium geometry and screenshot capture but has **NOT** been executed in this conversation. A DOM harness is not proof of rendered pixels or ASIO sound.

**Required before a production merge:**
1. Real Chromium at three viewports × three densities; screen capture source CH1 linking CH3–6, standalone Master control, all Route/SEND collapse states. No cable over fader or FX, no clipping, and legible gain pots.
2. Native test: CH1 mic → CH3/4/5/6, four independent plugin chains and faders, each receiver alone audible; mix-sum headroom and monitor path checked. Switch native mode while running with exact accepted revision.
3. Fail-closed: self-loop, multihop cycle, occupied physical input, invalid mono/stereo width, wrong device epoch, unplug, plugin crash, target delete, snapshot recall with stale routes.
4. Performance: source fanout should add no redundant hardware captures or unexpected ASIO latency; measure RT allocation, CPU budget and sample timing under 4/16/64 edges.

## Repository boundary

PR #278 stays Draft until owner visual approval. Prototype HTML and tests live inside `design-references/audition-mixer-study`; public Pages preview exports may follow after standalone smoke. Native code must conform to existing approved v5.2 ARD/PRD rather than copying this reference script directly.
