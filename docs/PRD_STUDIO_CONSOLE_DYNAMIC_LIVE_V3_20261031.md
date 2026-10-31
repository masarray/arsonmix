> **SUPERSEDED PRODUCT SCOPE — READ [PRD v4: Full Live Studio One Console Parity](PRD_ARSONMIX_LIVE_STUDIO_CONSOLE_PARITY_V4_20261031.md) FIRST.** This v3 document is retained as historical M3 engineering context only. Product is LIVE VST3 digital mixer, NOT timeline/arranger/audio editor. V4 adds full Channel/Bus/FX/VCA/Listen/Cue/Scenes/I/O parity and current merged #244 dynamic publisher status; all v3 "not yet merged" notes can be stale.

# ArSonMix PRD v3.0 — Studio Console workflow for REAL-TIME mixing
Date: 31 October 2026 · Primary product requirement · Owner: #170 / #192

## Purpose and benchmark
ArSonMix is a Windows-first low-latency LIVE digital mixer for stage, karaoke, podcast, broadcasting and user-configured ASIO/WASAPI, physical and virtual output destinations. The product is not a DAW timeline arranger/recorder and does not mandate OBS or virtual cable. Its everyday Console workflow should match the speed and capabilities of Studio One / Fender Studio Pro, with distinct premium ArSonMix gunmetal styling (no copyrighted asset cloning).
Official references:
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm

## Required user flows (binding acceptance, not feature claims)
1. ADD CHANNEL creates a real mono/stereo audio channel node with its own stable ID, live FX rack, fader, meter, pan, mute/solo, input source selector and primary output; it may start unpatched/muted. Physical pin count does not limit logical channel count. No fake browser-only tracks.
2. Multi-select CH1, CH2, CH3 with Ctrl/Shift → right-click → ADD BUS FOR SELECTED CHANNELS creates a NEW, independently numbered and named GROUP BUS and changes all three PRIMARY OUT destinations to the new bus in one atomic operator action. Bus PRIMARY OUT defaults to MAIN. The input channels retain their numbers, hardware pin and FX state; no input lane is consumed to create the BUS.
3. ADD BUS creates an empty summing BUS. ADD FX CHANNEL creates an independent parallel FX RETURN with native VST3 inserts. Sources can SEND PRE/POST to FX while primary OUT remains unchanged; a reasonable reverb plugin can be set to 100% wet by the operator, not hardwired as DSP.
4. Each common Console strip displays IN (hardware alias or bus fan-in count), OUT → selected primary destination, and active SEND(s) with PRE/POST/gain/pan. OUT NONE must not erase SENDs: if sends exist, label SEND ONLY. An FX Bus or Group Bus has input-source count with click-to-list, Select All sources, Show/Hide Sources. Selected Bus highlights real accepted senders and downstream destination. Optional Signal Flow overlays solid primary paths and dashed parallel sends; no permanent spaghetti between faders.
5. Universal OUTPUT picker accepts MAIN, an existing legal BUS, NONE and real validated destination/monitor paths where implemented; illegal cycles/feedback/double-dry paths are rejected before acceptance. BUS may send to another BUS as long as the graph remains acyclic.
6. All audio lanes CH1..N, group, FX and MAIN share ONE native processing/insert lifecycle and uniform Rack GUI where role-appropriate. Real vendor editor, bypass, move, Show chunks and Plugin Catalog state are consistent. Duplicate creates an independent VST instance, not a shared plugin object. VCA/DCA Group is control-only, NOT an audio Bus.
7. Show SAVE/RECALL: stable ChannelIds, names, ordering, source bindings, routes, effects/chunks and safe hardware endpoint rebinding survive restart; never silently arm unknown outputs.

## Visual and interaction baseline
- Keep original compact brushed gunmetal / steel, PNG faders, BTN-180, aligned meter/fader grid, MASTER pinned right, and low-end PC responsiveness. Distinct source/Group/FX role labels and tasteful color accents, zero artificial hover blur/glow, no oversized panels or repeated jargon. At 1366px bank or scroll horizontally, never cram 40 tiny strips.
- Source, OUT and SEND controls accessible from selected/individual strip in 1-2 meaningful actions. Advanced physical PATCH remains available separately, not required for routine BUS routing.
- Accepted native state vs pending DRAFT is explicit; activity LED/meter only for real native telemetry. Do not rerender all channel DOM nodes on each meter tick.

## Current SOURCE-VERIFIED truth
main at draft baseline 22e4fdf: existing single native 10CH+4 AUX mixer callback, M2B BUS borrows an unused CH strip, CH1 vendor FX special case, MASTER FX/PDC incomplete. DynamicChannelRoster and DynamicPreparedMixerBlock are OFFLINE precompiled foundations, not live JUCE channels. DEVICE4 named I/O remains production functionality. The new group/FX batch transaction is a CONTROL-THREAD feature and still cannot be clicked as live ADD BUS until native callback handoff is complete. CI green is not physical zero-FX FAT (#15).

## Feature milestone acceptance
M3: replace old fixed 10CH engine with ONE ChannelId-keyed live graph/FX instance owner preserving all existing device/Show/Orbit/monitor data. Physically patch Mic1/Mic2 to a NEW Vocal Bus (not borrowed CH10), then add a NEW FX Reverb return; preserve MAIN and AUX and reject stale ACK/feedback.
M4: full Console keyboard and right-click selected-channel workflow plus real BUS source list, OUT selector, highlighted accepted routing, dynamic strip views, safe Show of N channels; 1366/1440/1680 Chromium geometry and browser-vs-native differentiation.
M5: native MASTER FX/PDC/zero-FX crackle #15 mitigation with Windows hardware FAT: 44.1/48/96 kHz, 64/128/256 buffers, 30-minute soak, plugin state restore and device loss, driver telemetry and low-end responsiveness.
DONE = native PCM + exact-head Windows MSVC/CTest/ZIP + Chromium UI + actual physical FAT, never merely screenshots or source contracts.
