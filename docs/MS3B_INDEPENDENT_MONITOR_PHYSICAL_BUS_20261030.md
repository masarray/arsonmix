# MS3B — Real independent MAIN + MONITOR hardware output mix

**Source baseline:** merged G5E #197, MS3A #198, DYN1A #199, MS3A2 #201; MAIN is already physically patched and metered on the graph-selected callback ordinals.

## Delivered by this milestone

- ONE existing `LiveDryRoutingLab` callback and ONE `LiveChannelFxGraph` prepared topology. No second DSP renderer, VST3 execution, audio thread or rendering clock.
- Optional stereo **MONITOR** bus, independently mixed from 10 channel and 4 AUX **POST-fader** taps with per-lane scalar 0.0–1.0. Physical MAIN remains the native master post-processor and gain/pan output; MONITOR explicitly bypasses MAIN master gain and is NOT a copy of MAIN or a fake preview. CH muted/soloed gate, channel FX and AUX FX are shared with existing graph exactly once.
- Real sparse ASIO/WASAPI physical output IDs are resolved OFF RT to callback ordinals. Physical pair must be active, distinct, disjoint from MAIN; two-output hardware cannot enable MONITOR. Failed validation never publishes the new graph.
- Master OUTPUT MUTE + DIRECT device fencing, stable device epoch/revision, native structural APPLY + ACK and explicit user input patch authorisation remain authoritative. No implicit unmute; no output patch based on display labels or neighboring guessed pins.
- Scratch: no second per-channel buffer, no callback lock/heap/JSON/processor instantiation. MONITOR accumulates directly into its two exclusive output buffers with a bounded branch per active send. All non-owned output pins are zeroed; failed DSP execution clears **all** physical outputs.
- Operator: AUDIO I/O OUTPUTS selects verified MAIN/MONITOR hardware pins. Explicit MONITOR enable/disable and one collapsible CH1–10/AUX1–4 POST send panel, preserving L2 mixer geometry and not touching the FX rack. All changes stage through the existing `monitor.js` shared draft, and only the native ACK publishes live.
- Regression: native simultaneous PCM CH1→MAIN vs CH2→MONITOR, channel FX execution counts once, no callback heap, invalid output ownership; Chromium commands validate MAIN 5/6 and MONITOR 1/2 in one APPLY.

## Safety and intentional scope limits

- A MONITOR send is **POST-fader only**. Independent PRE monitor/cue tap, headphone volume/master trim, MON/MATRIX multiple buses, 5.1/surround, PDC, latency alignment, talkback, mute groups, real hardware measurements and polymorphic dynamic CH11+ live integration are **not yet delivered**. This is one additional, independent stereo monitor output—not Studio One's entire output matrix.
- Native portable Show recall deliberately does not assign hardware MONITOR pins from another computer; a recalled Show uses the current verified MAIN destination and leaves MONITOR disabled unless explicitly re-applied. No hidden restored cue sound into another speaker.
- Physical FAT: verify 2-out rejection, 8-out MAIN1/2+MON3/4, reversed/sparse masks, hotplug while armed, 0FX #15 dropout performance, and 2 hours of standard-PC stability before any public release claim.
- Code review must verify ownership, stale device masks, maximum 14 sends, native ACK, no callback allocations, and GUI/WebView2/standalone source parity. Exact-head Windows MSVC/CTest and Chromium required before merge. Keep CI runs to one deliberate final commit.

**Next:** MS3C output bus/patch policy and monitor PRE/headphone gain with physical FAT; DYN1B migration of offline dynamic PCM DAG to ONE live native renderer, then banked GUI only after runtime channel ACK. 
