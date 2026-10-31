# ArSonMiX — authoritative progress reconciliation (31 Oct 2026, ~21:10 WIB)

**Read-first factual snapshot — not a physical release certification.**
Audited GitHub `main@d638fa5e8fed165cb7b2eb1c1bf849a25aa774f2` after merged M3 #242 and P0 capture diagnostics #241. Product: one Windows-first realtime LIVE digital mixer for karaoke, stage, podcast, radio and arbitrary operator-selected physical/software playback endpoints. **Not** a timeline editor, DAW, WebAudio DSP experiment, OBS-only host, or zero-millisecond end-to-end latency promise.

This ledger supersedes *status snapshots* older than this head, including the top-of-file “after #234 / #235 OPEN” paragraph in AGENTS and docs/RECONCILED_PROGRESS_M1_M2_20261031.md. It **does not** supersede engineering safety rules or the current [PRD v3](PRD_STUDIO_CONSOLE_DYNAMIC_LIVE_V3_20261031.md), [ARD v3](ARD_STUDIO_CONSOLE_DYNAMIC_LIVE_V3_20261031.md), [M3–M5 handoff](HANDOFF_STUDIO_CONSOLE_M3_M5_20261031.md).

## Verified main capabilities vs live limits

| Domain | Actually merged / source-verified | Still absent or unqualified |
| --- | --- | --- |
| Native mixer | **ONE** JUCE AudioDeviceManager, one audio callback, fixed **10 physical input lanes + 4 internal AUX returns**, MAIN, independent physical MONITOR and optional clock-bridged secondary playback destination. Native route epoch/revision ACK and guarded MUTE/DISARM on topology changes. | Live runtime-added CH11+, arbitrary numbers of independent buses, general multidevice input capture, professional physical loopback/FAT. |
| Live controls | #223 real smoothed (<=64 samples) existing CH→AUX POST send gain; #231 AUX return master gain; #238 unified live changes in focused SEND editor; #236 coalesced live AUX drag; source switch where already patched and same-format. | Creating/deleting sends, channel format, physical patches, topology edits still require MUTE→APPLY→native ACK. No unrestricted live graph edits. |
| Studio I/O | #228 custom backend/device/rate/buffer/pin apply; #234 cached inventory with guarded rescan; #239 ASIO vs Windows Audio + actual M/L/R crosspoint; #240 DEVICE4 named mono/stereo logical input **aliases** bound to real reported device pins; #241 truthful RAW input PCM diagnosis while output muted. | “64 pins” is **hardware pin selection capacity**, not 64 native channels; named aliases are not actual new PCM channels; independent Windows capture devices cannot be silently combined. |
| FX and existing buses | #235 unified CH1–10 FX rack presentation, real bounded +BUS / +FX BUS routing by borrowing an unused live strip; current VST3 native ownership and Show must be preserved. | No independent **live** arbitrary GROUP/FX nodes beyond 10CH; CH1 legacy VST lease remains special, MASTER native FX/PDC/sidechain parity incomplete; no full LV1 DCA/MATRIX/CUE. |
| Dynamic Studio bus foundation | **#242 merged**: `DynamicChannelRoster::createBusForSelected`, exclusive `primaryOutput`, distinct parallel sends and real two-input PCM Group/FX Return regression on offline `DynamicPreparedMixerBlock`. Invalid selection, revision and budget fail closed; migration avoids doubled direct MAIN. | **OFFLINE CONTROL + PCM ONLY**. Not wired to production `LiveDryRoutingLab` callback, not a working user-facing ADD BUS/ADD CHANNEL menu, and not independently persisted live Show/plugin state. |
| User interface | Pinned MASTER, L2/RAIL3 aligned faders/meters, device-first I/O matrix, focused SOURCE→FX→SENDS→OUTPUT, one routing authority, native-ACKed SEND control. | Comprehensive low-end integrated-GPU pointer and long-run Windows FAT still not established. |

## Current development / CI ledger

- **Main** at audit: [d638fa5e](https://github.com/masarray/arsonmix/commit/d638fa5e8fed165cb7b2eb1c1bf849a25aa774f2) = merged [#242](https://github.com/masarray/arsonmix/pull/242). Chromium main [#41208906041](https://github.com/masarray/arsonmix/actions/runs/41208906041) **SUCCESS** at audit; Windows main [#41208906032](https://github.com/masarray/arsonmix/actions/runs/41208906032) **IN PROGRESS** at audit; check final run state before claiming exact-main package qualified.
- Last earlier combined main baseline **19ebb466** after [#241](https://github.com/masarray/arsonmix/pull/241): Chromium [#41204771543](https://github.com/masarray/arsonmix/actions/runs/41204771543) and Windows native [#41204771528](https://github.com/masarray/arsonmix/actions/runs/41204771528) both **SUCCESS**. This does not qualify #242 head.
- **OPEN [#243 FAST1](https://github.com/masarray/arsonmix/pull/243)**: one-click First Sound (verified native input patch ACK → explicit output ARM ACK + panic MUTE, and factual signal status), not yet on main. Its **PR head `50cb526b`** passes [Chromium #41205403390](https://github.com/masarray/arsonmix/actions/runs/41205403390) and [Windows #41205403498](https://github.com/masarray/arsonmix/actions/runs/41205403498). **DO NOT equate PR-head green with integration onto latest main**: compare shows diverged (22 ahead / 13 behind main), GitHub currently calls it mergeable. Rebase/reconcile narrow changes to current main, protect #242 roster, #241 raw capture, DEVICE4, live send gain, then exact integrated CI before merge. This FAST1 feature explicitly ARM-requests only after user clicks and native graph ACK; never auto-unmute on device reconnect/Show.
- **OPEN old-base [#229](https://github.com/masarray/arsonmix/pull/229) and [#225](https://github.com/masarray/arsonmix/pull/225)** FactoryFX/ArSonComp sources overlap. Do not merge both wholesale. `config/factoryfx-approved-releases.json` still has `"plugins":[]`: no approved bundled factory binary. A source/binary is not factory-qualified simply because its sibling CI passed.
- Remaining older open PRs are historical/stale or alternate architectural prototypes; retain their history but do not bulk-merge old `Main.cpp`, `monitor.js` and audio callbacks.

## P0 release blockers / product truth

1. **[Issue #15](https://github.com/masarray/arsonmix/issues/15)** hardware crackling **with 0 VST3** is still OPEN. Last physical evidence predates recent fixes; software CI, simulated PCM vectors and callback CPU usage cannot establish crackle-free output or actual round-trip latency. #241 raw PCM under MUTE helps isolate silence vs output gate, but does NOT solve physical audio or crackle by itself.
2. First audible customer flow must be tested on real Windows ASIO and WASAPI devices: select verified capture device, prove raw PCM is present, patch MIC/virtual input to CH, receive native route ACK, explicit hardware ARM, hear MAIN L/R; investigate virtual cable direction, master and output mute, xrun/clock starvation, rearm and reconnect. Start with **0 FX**, 44.1/48 kHz, known buffer configurations and lower monitoring level.
3. Maintain honest measurable **near-zero extra mixer DSP latency** goal, not blanket 0 ms physical latency; secondary independent-clock path necessarily adds buffering/ASRC.

## Next meaningful milestone order (no duplicate engines / CI waste)

**Gate A — FAST1 integration + first audible field-test path:** reconcile #243 on current main; exact integrated Chromium + Windows native CTest/ZIP, then user physical INPUT→CH→MAIN/OUTPUT and 0FX #15 FAT. Keep one native route ownership and ARM safety. Never display LIVE from JS intent alone.

**Gate B — M3-LIVE single prepared ChannelId graph cutover:** migrate 10CH+4 AUX+MAIN/MONITOR into `DynamicPreparedMixerBlock` inside **the existing one JUCE callback**, with real FX instance leases and CH1/Orbit/editor/vendor Show preservation; validate legacy PCM A/B, gain smoothing, pending VST state and epoch/revision/rollback. Do not graft a parallel callback.

**Gate C — true Studio Console Add Channel / Add Bus / Add FX Channel:** after Gate B, wire atomic #242 control roster/batch to native session ACK, then provide selected-channel multi-select/right-click, independent named bus strip, explicit IN/primary OUT/parallel SEND, source fan-in popover, virtualized bank, and independent FX rack. No borrowing CH10 in the final product.

**Gate D — production FX, Show, performance:** per-ChannelId VST3/MASTER leases + PDC, safe live editor/reorder, factory legally approved SHA/UID locked binary packaging, full Show/Recall Safe, low-end GPU profiling and hardware soak.

## Ownership and rules for next agent

- Start from current main and active PR heads; compare ancestry and file ownership before touching source. Old docs/AGENTS milestones below this snapshot are history, not instructions to reset the current architecture.
- Keep single JUCE device owner, one callback, immutable off-RT prepared graph; no RT heap, blocking mutex, JSON/device scan or VST instantiation. Use preallocated data and bounded epoch/revision ACK publication.
- Preserve compact premium gunmetal mixer appearance; no blur/filter mouse-hover workload, no fader geometry change without evidence. Product is audio-reactive **real-time** digital mixer, not DAW.
- CI economy: prefer ONE exact integrated Windows build/CTest/ZIP + Chromium per meaningful milestone after cheap local syntax/source-contract tests. Never count green old-head CI as release qualified. Physical FAT is independent.
