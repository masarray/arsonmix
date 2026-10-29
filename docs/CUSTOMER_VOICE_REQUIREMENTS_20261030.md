# ArSonMix — Voice of Customer (VoC): YouTube Feedback -> Testable Product Requirements
Date: 2026-10-30 | Status: PRODUCT PRIORITIZATION EVIDENCE; NOT A PROMISE OF DELIVERY
Evidence: user-supplied pasted YouTube comments in chat, 2026-10-30; source is an exported text attachment, not an independently authenticated public comment feed. Handles are references to that attachment, and no user comments are falsely attributed to repository users.
Companions: PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md; GUI_WORKFLOW_BENCHMARK_20261030.md; ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md.

## A. Interpretation rules
Do not count praise, "link download", and requests for platform support as proof that requested DSP already exists or that software is release ready. The most repeated, high-impact unmet *technical* expectation is multichannel device I/O and independently routable outputs, followed by shared FX sends/returns and user-friendly flexible channels. Keep technical need, actual native status, proposal, and test separate. P0/P1/P2 below are this audit's proposed ordering, not previously created GitHub priority labels.

## B. Source traceability & implementation gaps
| ID / feedback | Explicit need from attached user comment (paraphrased) | Actual source baseline at main@527e155 | Target acceptance / priority |
|---|---|---|---|
| VOC-01 @marjanadi6472 | Additional channels beside master for vocal, sub, monitor with external multichannel interface | 10 input fixed; AUX internal and MAIN HW1/2 only | Logical bus names/types and output patch; separate MON/SUB, P0 G4 |
| VOC-02 @yc0nyo | ASIO I/O follows device; channel count customizable; one source can feed several destinations; comment mentions at least eight FX insert/send facilities (ambiguous whether insert slots or send slots) | Real DIRECT ASIO/WASAPI pin inventory and 10 native channels; CH1 up to 8 VST3, others 1; no bus->HW multiout | 8x8 test, safe fan-out, configurable bounded capacity plan, shared per-channel multi-insert; P0 routing then P1 FX |
| VOC-03 @syamsudinmhd7440 | Speaker, internal/headphone output separated | MAIN stereo only | Independent MONITOR bus + explicit HW3/4 when available; refuse on 2-output, P0 |
| VOC-04 @inkblackdeath7125 | FX send/return or FX1/FX2 shared by many tracks to save CPU/delay | 4 real stereo AUX FX returns and PRE/POST source sends, but daily MIX controls incomplete | 2+ MIC sends -> one actual reverb VST, observable instance count/CPU and correct wet returns, P0 G3 |
| VOC-05 @rofiqarroudhloh4801 | Additional AUX/FX channels | 4 fixed AUX return | Configurable bounded bus-capacity proposal after benchmark, not phantom new outputs, P1 |
| VOC-06 @protech_production | Performer/SoundGrid-like I/O, AUX, GROUP similar to LV1/DiGiCo | 10CH + 4 AUX internal, GROUP/DCA/MATRIX/monitor incomplete | Typed FX AUX / GROUP / MON / DCA / MATRIX entities and appropriate routing tests, P1 after G4 |
| VOC-07 @iradang-6097 | Matrix routing | No typed MATRIX processing/output | Matrix bus with independently patchable outputs and safety/PDC, P1/P2 |
| VOC-08 @Abay123-j3v / @gerisonp.upongantung6713 | Support 4-channel soundcard and general hardware configurations | Device backend exists; independent outputs not user-assignable | Tested 2x2, 4x4, sparse 8x8 and backend mismatch, P0 |
| VOC-09 @agustronstudio7679 | Effect roundtrip with external analog mixer | Not a validated external FX loop | Hardware SEND -> DAC output -> external insert -> ADC return, clock/latency/feedback safeguards; P2, after multiout |
| VOC-10 @SaptaKaisagi | Audio player, playlists, setlists | No validated native player/transport subsystem | Separate transport/source node, playlists/setlists and safe preloading, P1 performer |
| VOC-11 @Yogijuliana20 | Multitrack playback | No native multi-stem playback established | Multi-stem files as logical mixer sources sharing device clock, P1/P2 |
| VOC-12 @LoveGamingforlife | Heavy CPU/RAM benchmarks and film/music/vocal presets | Technical telemetry exists; no complete representative benchmark/user preset library evidence | Report realtime budget p95/p99, CPU/RAM, sample-rate/buffer, VST set; curated legally distributable presets, P1 |
| VOC-13 @JoeSherin-n7v6w | Studio-like vocal clarity without harshness | Third-party VST host and partial native preset foundation | Tested vocal preset A/B and level-matched listening, avoid claims of subjective guarantee, P1 |
| VOC-14 @oldiesbutgoodiesremix / @chachashifa4790 | Pitch/transpose/speed/harmony | Not built-in as full DSP subsystem | Host compatible VST3 first; original pitch/speed/harmony future separate DSP plan, P2 |
| VOC-15 @jonikaofficial / @FalenzGolgota94 | Work with another DAW as plugin | Windows standalone host | New VST3 plugin-target lifecycle if justified; do not claim EXE is VST, P2 |
| VOC-16 @RoyPardi | VST2 support | VST3 primary | Legal/SDK/licensing feasibility first, no implicit VST2 promise, P2 |
| VOC-17 @BambangWX / @jainursantoso / Android comments | macOS/Linux/Android versions | Native Windows WebView2/JUCE baseline | Separate platform backlog, backend viability and UX/performance proof, P2 |
| VOC-18 @cowbra7745 / Windows 7 comment | Win10/Win7 support | Windows baseline; compatibility not fully certified by comments | Define actual supported OS matrix from vendor SDK & artifact tests, no blanket Win7 promise, P1 |
| VOC-19 @onecelebes / @adhypermana | DAW expectations and "unlimited tracks" | No DAW sequencer; fixed 10CH; Windows-first live mixer | Product positioning as live mixer, bounded scalable channels, no unsupported unlimited claim, P1 |
| VOC-20 @many commenters requesting download/tutorial | Quick install, reliable usage, accessible documentation | GitHub release/Actions and browser preview exist; real 0-FX hardware quality not certified | Reproducible provenance ZIP + straightforward device/first-sound tutorial only after gate #15, P0 release |
| VOC-21 @MD.AUDIO.CHANEL / karaoke and worship commenters | YouTube / computer audio / streaming / karaoke / mosque output | Native device routing exists but OS loopback/stream capture is not universally supported or guaranteed | Explicit legal/capability-aware loopback device setup, select stereo capture path, P1 |
| VOC-22 @5knowledge22 | ArSonKuPik browser capture extension should release capture when off | Separate browser extension, not ArSonMix native mixer itself | Cross-product backlog outside this repo; do NOT silently scope as mixer bug, separate handoff |
| VOC-23 @LoveGamingforlife | Requests code be made open-source | Repository and AGPL-3.0-only license notice already publicly available | Maintain clear source license, third-party SDK and plugin redistribution boundary; existing project state, not new DSP milestone |

No percentage/customer-count ranking is inferred from this unstructured sample. Users may make overlapping requests and many comments are praise or availability questions.

## C. Target user personas and "first successful use"
- Karaoke/KTV performer: MIC and MUSIC distinct logical input sources -> own EQ/compression insert -> shared AUX1 Reverb -> MAIN, save portable preset; no need to understand device callback port indices.
- Live FOH engineer: choose external ASIO card, name stereo/mono input, group 4 vocal channels, monitor performer separately from MAIN, route every bus to qualified output pins, diagnose no-sound through focused trace.
- Streaming/voice: route microphone and playback to independent main, headphone and recording/virtual destinations only where actual backend supports; audible monitoring independent of stream path.
- Portable VST studio: import owned VST3 bundles into user-managed vault, backup show, restore on different PC with user-approved hardware remap, respect vendor plugin activation.
- Minimal integrated-GPU user: no pointer stutter, false lit LEDs, browser white flash, giant noisy modal cards or VST list disappearance.

## D. Concrete release requirements and gates (not implementation claims)
P0 release integrity: existing issue #15 0-VST physical crackle is open; zero-FX/WASAPI/ASIO at actual devices with honest discontinuity reporting; don't ship "production stable" based on unit/CI.
G1/G2 PR #167 MERGED: FX/ROUTE/SENDS and custom INPUTS/OUTPUTS/DEVICE GUI are implemented with native input patch and output readback; preserve #166 CH1 ADD. Full custom hardware mode/rate/buffer selection, independent physical output patch, G3 send controls and real hardware FAT remain unfinished.
G3 shared FX: at least two independent microphone sources feed one AUX reverb in PRE and POST conditions, FX return has real fader/mute/meters, send inspector visible from MIX and PATCH, actual ACK.
G4 true multiout: verified 8-output device MAIN -> 1/2 and MON -> 3/4 simultaneous (then SUB/REC), 2-output rejects unavailable destinations, dedicated bus naming, no silence/feedback regression.
G5 multi-insert/PDC: same actual native per-strip multi-VST chain and safe latency compensation for CH2–10/AUX, with FX state persistence and closed-loop hardware FAT.
Later: configurable channel count, GROUP/DCA/MATRIX, playlist/setlist/multitrack, platform/DAW target feasibility, user presets and performance metrics.

## E. Engineering acceptance checklist
1. No UI-only "successful" output patch: native status readback and auditioned HW pins must agree on capable hardware.
2. No semantic alias: FX AUX RETURN != hardware AUX OUT != GROUP != MATRIX != MON/CUE != DCA. DCA is gain control, not summing audio.
3. One sender can fan out to MAIN+several verified buses without accidental duplicate summing; route graph rejects cycles, ambiguous hardware ownership, mismatched stereo/mono pins.
4. Prepare FX/output graph off RT; no allocation, plugin construction, filesystem or locks inside callback; PDC aligns parallel paths.
5. All structural changes use bounded DRAFT -> PENDING -> native ACK LIVE / REJECTED, device epoch/revision and output MUTE gate; failure preserves old accepted topology; no implicit unmute.
6. UI shows hardware capability, not fabricated channel count. Browser preview DEMO distinctly marked; exact native 0-FX hardware pass remains mandatory.
7. Distinguish WORKING, PARTIAL, NOT IMPLEMENTED, PLANNED, and DRAFT PR in release communication. Do not convert sample comments into promised feature dates.

## F. Cross-links
Existing canonical constraints: PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md; ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md; FX_RACK_ROUTING_CROSS_THREAD_CONTRACT_20261030.md.
Current delivery: PR #166 merged, PR #167 merged, issue #15 hardware safety; existing issue #109 for routed channel/sends and #143 for unified patch. Reuse those; do not create redundant per-comment issue spam.
