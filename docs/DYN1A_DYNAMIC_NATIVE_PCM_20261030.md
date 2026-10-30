# DYN1A — Runtime-Sized PCM Render Kernel (2026-10-30)

Owner epic [#192](https://github.com/masarray/arsonmix/issues/192). Current baseline: merged DYN0, G5C/G5D, MS2A. This is a **native DSP source-code + regression** milestone, not a LIVE Add Channel implementation and not DAW parity.

## Implemented

- \`DynamicPreparedMixerBlock.h\` is an offline, replacement-ready successor kernel to fixed \`PreparedMixerBlock\`. It is deliberately NOT wired to the JUCE callback in parallel, so NO second live renderer exists.
- One reusable lane definition and one \`processFx(ChannelId,...)\` interface for CH1 through CH128, with ChannelId stable across reorder, same mono/stereo/fader/pan semantics, INPUT and MAIN roles, explicit ID-based send edges, sample-accurate PRE/POST accumulation, mono equal-power send projection.
- Control-thread-only O(V+E) topological compile, DAG rejection, validation of callback physical-pin ordinals, stereo source pair, channel ID, exactly-one MASTER. Kahn sorted CSR edges with runtime dense indices; no per-block map, graph sort, topology mutation or VST construction.
- Control admission checks max block quantum, physical count and scratch-byte budget. The compiler accepts 128 configured lanes with five active inputs and 1024 logical mostly-disconnected lanes; only lanes reachable to MAIN allocate callback scratch (1024 dormant descriptors cost only 1 stereo MAIN buffer). A failed graph compile leaves the prior processed audio graph intact.
- Native C++20 test runs actual PCM for CH1, CH11, CH32, CH64, CH127 and MAIN, checks stereo master gain, FX delegate identity, reordering, PRE-fader vs MUTE/SOLO, fader zero, input duplicate and cycle rollback, 1024-channel lazy scratch and failed memory admission, through the **existing** ARMixMixerRoutingTests target. No extra job.

## Remaining before + CHANNEL is LIVE

DYN1B must replace the fixed 10 input+4 AUX graph in \`LiveDryRoutingLab\` with the runtime-sized kernel **in the existing callback**. Requires regression covering full existing native route schema/Show V4, source modes + sparse driver pins, CH1 legacy Orbit/editor state handoff, actual JUCE VST3 instance bank on ChannelId, bus/group/SOLO semantics, native MUTE/APPLY/epoch-revision ACK, live fader smoothing and no-vendor-release on RT. No premature live integration; replacing existing algorithm must preserve actual 0FX and VST3 output. Current DYN1A is only PCM gain/graph evidence with delegate FX stand-ins, not native VST3 CH64.

DYN2 must add native-ACKed ADD/REMOVE/DUPLICATE and event-driven banked/virtualized UI, then Show V5 and legacy migration. Hardware #15 0FX crackle qualification and PDC remain independent blockers. Cross-thread dependencies: typed MS2 #174, output patch #198, FX Show #197. No UI buttons unless accepted graph is live.

## Compatibility and safety

No modifications to current production JUCE callback, legacy 10CH native route command, FX VST3 lifecycle, physical output patch, Show V4 or L2 GUI. The transition is deliberately protected by a compiled PCM test and unchanged existing 10CH tests. There is no product-limit constant 10/12 in the new source; practical capability depends on DSP CPU and memory admission, not unbounded hardware promises.
