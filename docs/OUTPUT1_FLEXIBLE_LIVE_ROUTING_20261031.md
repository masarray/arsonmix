# OUTPUT1 — Flexible live mixer output routing (31 October 2026)

**Product direction:** ArSonMix is a real-time software digital console for live performance, karaoke, stage sound, podcasts, broadcast and streaming. It is not a DAW, audio editor, OBS-specific tool, or mandatory virtual-audio-cable utility. Studio One/Fender Studio Pro and Waves eMotion LV1 are usability, routing, reliability and FX references.

## Audio architecture

Logical signal path:

**Selected physical/software INPUT (ASIO/WASAPI) → CH1…N channel DSP / inserts / sends / AUX / GROUP → MAIN and MONITOR logical buses → native output routing → chosen device/output port(s).**

Output and input are abstract endpoints configured by the operator. An external live-streaming application, physical FOH desk, headphones, headphone amplifier, monitor speaker, USB interface or virtual-cable device is just a possible endpoint.

- **DIRECT shared-clock, lowest possible latency:** route MAIN L/R to any validated physical output pins on the active ASIO/WASAPI interface using the existing native matrix. No extra FIFO/SRC in this single-device path. Merged MS3B #203 independently maps MONITOR to another disjoint physical stereo pair using the same graph.
- **Optional SECONDARY stereo output (OUTPUT1)**: duplicate the actual same native MAIN PCM to one *user-selected*, uniquely identified Windows Audio playback endpoint, regardless of whether it is headphones, another audio interface, or a software playback driver. An independent render callback **only consumes PCM**; it never creates a second mixing graph or processes the VST3 chain twice.
- **Cross-clock honesty:** separate devices have independent clocks, so use a bounded SPSC FIFO plus band-limited AdaptiveClockBridge/ASRC prepared on the control thread. Additional latency is physical/algorithmic and nonzero; the selected endpoint displays negotiated output rate, bridge target buffering and underflow/overflow. No unmeasured round-trip or zero-millisecond claim.
- **Native routing control:** in existing PATCH → OUTPUTS, user discovers real Windows playback device names, selects the secondary destination, CONNECT, ARM/DISARM OUTPUT and DISCONNECT. The direct MAIN path does not require any secondary endpoint. Output monitoring mute and secondary output arm are independently controllable, so live audio can feed a selected device while FOH speakers are muted. No implicit auto routing to unrecognized devices, and no automatic rearm after reconnect.
- **Structural safety:** MUTE physical hardware and DISARM secondary outputs before route APPLY, Show recall or preset capture. A re-arm first resets previous FIFO/FIR cursor/history only with producer and consumer callback leases quiescent, avoiding replay of old PCM. Device stop/hotplug invalidates secondary clock. Endpoint Core Audio IMMDevice identity and stereo mask are verified pre/post open.
- **RT constraints:** no heap allocations, device opens, locks, filesystem work, JSON, plugin construction or teardown in the producer or output callback. One mixed PCM source, bounded clock bridge, callback-level output gate and control-thread resource release.

## Explicit limitations / next integration gates

OUTPUT1 currently duplicates **MAIN only** to **one** secondary stereo WASAPI playback endpoint. A fully general multi-device bus/destination matrix, alternate ASIO output drivers, independent GROUP/AUX-to-secondary routes, headphone CUE/PFL/AFL, per-output delay/PDC and unlimited endpoint counts are **not yet implemented**. Never claim full eMotion LV1 routing parity. Native MS3B independent MONITOR (#203) is integrated here; DYN1B live runtime-sized graph and DYN2 user channel management remain future milestones, preserving the existing L2 GUI.

The user decides whether to connect this output to speakers, amplifiers, an external recording device, or streaming software. Do not embed assumptions about OBS, cable brands, or online encoder workflows in the native mixer.

## Hardware acceptance

Exact-head MSVC native CTest and Chromium first. Then verify actual low-latency ASIO input → FOH/monitor across interface output pins, dual selected playback device 44.1/48k ASRC, stage-mute behavior, stale FIFO after rearm, device identity and hotplug fail closed, multichannel FX bypass and PDC interaction, CPU/xrun evidence and physical round-trip/measurement. Existing #15 hardware crackle FAT remains a release blocker.

## MS3B/OUTPUT1 physical mute vs streaming meter reconciliation
When secondary-only streaming is armed, post-FX MAIN is published once to the secondary device FIFO, then measured for MAIN loudness/peak before muted hardware MAIN and MONITOR buffers are cleared. When hardware output is armed, the physical MAIN/MONITOR crosspatch remains active. Routing APPLY/Show requires physical MUTE and secondary DISARM; the frontend rejects the same unsafe state. This is one existing JUCE mixing callback and no extra native VST3 processing.
