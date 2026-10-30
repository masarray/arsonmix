# DYN1B-1 — Dual-Sink Dynamic PCM Graph, Runtime ChannelId Engine
Date: 2026-10-31 · Owner epic #192

## What is implemented
The existing, device-independent DYN1A graph now computes MAIN and independent MONITOR from **one immutable schedule** with runtime-sized ChannelId lanes and no second FX traversal. This completes the missing two-bus kernel prerequisite for the one-JUCE-callback migration.

- Exactly one MAIN sink required and at most one optional MONITOR sink. A MONITOR feedback send, duplicate MONITOR, cyclical routing, invalid physical patch or memory-budget violation rejects the new compile while the previously admitted renderer stays available.
- A reverse graph reachability pass starts from **both** sinks, so a channel sent only to MONITOR is included; a disconnected 1024-channel description still incurs only the scratch for actually audible lanes and sinks.
- Audio process accepts distinct caller-owned MONITOR L/R buffers, refuses missing/aliasing pairs, computes all source inserts once, and keeps MAIN mute independent of MONITOR output. Physical driver pin selection remains the existing native verified port matrix and is not invented here.
- Tests generate 64 normal channels plus MAIN/MONITOR, physically distinct PCM, monitor-only CH32, main-only CH64, reorder stable IDs, duplicate sink and feedback rejection/rollback. Existing DYN1A 128/1024-channel lazy-memory suite still runs in **ARMixMixerRoutingTests**, avoiding extra CI jobs.

## Critical status boundary
**This is not yet live dynamic +CHANNEL**. JUCE `LiveDryRoutingLab` and `LiveChannelFxGraph` still process the legacy fixed 10 input + 4 FX AUX for actual audio. DYN1B-2 must migrate legacy source format, PRE/POST AUX return, per-channel VST3 instance and vendor/editor Show state into the one new kernel, then publish at the current MUTE/APPLY/native epoch-revision ACK boundary. The original CH1 plugin/Orbit state must be conserved. DYN2 may expose virtualized +CHANNEL only after that native integration test passes. Do not claim an actual 66-channel VST3 mixer yet.

## Hard gates
Native C++ compile/CTest exact-head; compare old 10CH MAIN/MONITOR PCM and FX returns, BUS sends, mute/solo, smoothing, SHOW V4 recall, 2-output vs 8-output hardware and single callback execution. Verify 0 FX crackle #15 on physical Windows hardware independently. No RT allocations, locks, GUI-driven device opens, or implicit port assignment.
