# Studio One / Audition / eMotion LV1 → ArSonMiX feature evidence ledger v4.1 (2026-11-01)

> **CURRENT STATUS OVERLAY — 2026-11-01, \`main@9f66d085\` (supersedes snapshot table below):** FAST2 #259/FAST3 single AUDIO I/O APPLY **merged** (no START AUDIO); #258 independent stereo L/R native meters + FX entry and #260 atomic fader/PAN **merged**; #261 common CH1 native empty rack + independently staged multi-channel native FX Console **merged** (post-merge Chromium PASS, combined Windows pending at audit). Real production callback is still fixed **10CH+4AUX with occupied CH1 legacy VST special-case**. \`DynamicPreparedMixerBlock\` etc remain offline until full-fidelity production cutover. #263 one dry GROUP callback is **DRAFT/PILOT**; it does not support FX/AUX/sends/MONITOR/Show or CH11+ dynamic inputs. #251 WASAPI de-click is experimental default-OFF. FactoryFX approved plugin list empty, issue #15 0FX physical crackle FAT OPEN. The v4 table below is a **2026-10-31 historical truth table**, not a live feature inventory. The binding current plan is [PRD v4.1](PRD_ARSONMIX_LIVE_STUDIO_CONSOLE_PARITY_V4_20261031.md), [ARD v4.1](ARD_ARSONMIX_LIVE_STUDIO_CONSOLE_PARITY_V4_20261031.md), [Handoff v4.1](HANDOFF_ARSONMIX_STUDIO_CONSOLE_V4_20261031.md).

**Changed acceptance priority from the original table:** HW04 now uses one APPLY; FX01 has merged #261 UI/native draft ownership but still lacks full ChannelId CH1..N parity; CH01/BUS01/BUS02/BUS03 remain **unqualified end-to-end** (restricted #263 GROUP cannot replace the normal live mix); FX02/PDC/Listen and dynamic Show still pending; RELEASE remains hardware-BLOCKED. Do not close these lines because of a screenshot, offline PCM proof or single dry Group.

---

**Historical snapshot source:** `main@6022a4fc0218decac694568d73c772a634eba37a`. This is an engineering audit of repo code and merged-PR evidence, NOT a rendered-GUI screenshot inspection or physical FAT certificate. Refresh main SHA whenever this file is used. Benchmark only applicable realtime Console features. **Legend:** LIVE=merged runtime capability; PARTIAL=limited true native; OFFLINE=unit-tested control/PCM not in app callback; ABSENT=not implemented; FAT=physical proof missing.

| ID | Studio Console equivalent (actual expected workflow) | ArSonMiX status at snapshot | Evidence / necessary next step | Priority |
| --- | --- | --- | --- | --- |
| HW01 | Select installed ASIO/Windows Audio devices, rate/buffer, verified pins | LIVE; hardware FAT missing | #228/#234/#239; `src/Main.cpp`, `web/mixer-io.js` | P0 |
| HW02 | Studio named mono/stereo software input→hardware M/L/R matrix | PARTIAL: aliases and 10 live CH, no general dynamic software I/O | #240; persisted device-scoped aliases in `web/mixer-io.js` | P0 |
| HW03 | Software named mono/stereo *output* / sub-out matrix | PARTIAL: MAIN/MONITOR ports and secondary | #239; dynamic Sub Out lanes + portable mapping needed | P1 |
| HW04 | First-sound route+ARM in one deliberate workflow | LIVE software; no physical pass | #243 merged; native ACK plus user click; test real capture→speaker | P0 |
| HW05 | Input PCM, output, latency, xrun and device-recovery telemetry | PARTIAL | #241 raw capture under MUTE, issue #15; add physical xrun/starvation/loopback | P0 |
| HW06 | User picks physical or virtual outputs independently of app purpose | PARTIAL supported physical MAIN/MONITOR + optional second consumer | Preserve independent-clock ASRC; don't require OBS/cable | P0 |
| HW07 | Import/export portable I/O profiles remapped safely across interfaces | PARTIAL/UNVERIFIED | DEVICE4 aliases do not constitute full portable input+output setup import/export | P1 |
| CH01 | Add mono/stereo processing channel independent from input pins | OFFLINE foundation | #242/#244 roster/publisher; live fixed 10CH `LiveDryRoutingLab` | P0 |
| CH02 | Rename, color, reorder, show/hide, filter/search channels | PARTIAL GUI for fixed channels | `web/app.js` static model; stable ID reorder/filter not live | P1 |
| CH03 | Multi-select / batch copy-paste / batch route | ABSENT native end-to-end | Build selection on ChannelId, transactional native batch | P0/P1 |
| CH04 | Input gain, polarity, HPF, mono/stereo format independent | PARTIAL; verify actual native per-feature DSP | Existing UI has preview elements; ensure native tap order+stereo parity | P1 |
| CH05 | Fader, pan, mute, solo with true DSP and smoothing | PARTIAL native, fixed graph | #223/#231; transfer all lane realtime params into dynamic graph | P0 |
| CH06 | Channel editor / overview, focused routing/insert/send/cue | PARTIAL selected focus and unified FX Rack | #235; one native-backed channel inspector + full Cue | P1 |
| BUS01 | Add new *independent* GROUP Bus and nested buses | OFFLINE foundation; LIVE workaround borrows CH | #235 spare lane, #242 bus batch; dynamic live cutover required | P0 |
| BUS02 | Add Bus for Selected Channels; replace primary OUT exclusively | OFFLINE C++ tested | `DynamicChannelRoster::createBusForSelected` + golden PCM | P0 |
| BUS03 | Add FX Channel from Send drag, dry intact | OFFLINE FX Return foundation, AUX live fixed | #242 dynamic FX batch; native VST instance & new live output | P0 |
| BUS04 | Distinct PRE/POST Sends, levels, pan and SENDS on Faders | PARTIAL true native live POST gain | #223/#238; existing send gain <=64 samples; create/remove structural | P0 |
| BUS05 | Clickable IN/OUT, Bus fan-in popover and Hide/Show Sources | PARTIAL Focus/patch, absent dynamic fan-in UI | Graph snapshot and keyed strips after live cutover | P1 |
| BUS06 | Bus feedback prevention, multiple send destinations, OUT NONE | OFFLINE dynamic compiler, fixed native DAG | #242 and `DynamicPreparedMixerBlock`; callback parity | P0 |
| BUS07 | VCA control fader (not Bus), link groups, solo/mute group | ABSENT | Separate control nodes and one RT atomic parameter plane | P1 |
| FX01 | VST3 rack and actual effects on input and return | PARTIAL live; CH1 has legacy special lease | #235 common presentation, `processLegacyChannelOne` still exists | P0 |
| FX02 | Independent native MASTER insert/PAN processing | ABSENT | `web/bridge.js` explicitly disables MASTER FX and pan; no fake unlock | P0/P1 |
| FX03 | Plugin editor, bypass, move, real presets/chain, copy/paste | PARTIAL host/routed FX + Show | Unified InsertId leases, vendor chunks and cross-role support | P1 |
| FX04 | True sidechain sends and multichannel plugin bus negotiation | PARTIAL/UNQUALIFIED | Type safe edges, JUCE VST3 buses/layout and verified PCM | P1 |
| FX05 | Serial and parallel Splitter, frequency-band processing | ABSENT | Original DSP implementation with PDC; don't copy proprietary FX | P2 |
| FX06 | Latency compensation for branched buses/FX and hardware inserts | ABSENT / gated for CH1 latent FX | `LiveAudioEngine.h` refuses nonzero-latency CH1 in native sends | P0/P1 |
| FX07 | External hardware insert with measured round trip | ABSENT | P2 device-bound send/return + feedback protection + PDC | P2 |
| SRC01 | Live external instrument/Aux input and optional VST3 instrument PCM source, no MIDI timeline | PARTIAL physical Aux/route; VSTi generation ABSENT | Separate VSTi class + safe realtime MIDI controller + audio-output ChannelId, not an arranger | P2 |
| IO08 | Surround/multichannel software I/O channel modes where device/plugin supports it | ABSENT beyond current mono/stereo plan | Format-aware dynamic graph and per-bus VST3 negotiation after stereo parity | P2 |
| MON00 | Talkback/dim/mono check tied to actual Cue/Monitor hardware | PARTIAL Master mono; no verified Talkback | Explicit mic-to-Cue route, anti-feedback and momentary-safe controls | P1/P2 |
| MON01 | Independent MAIN and MONITOR physical buses | PARTIAL LIVE | Existing callback native MAIN/MONITOR; full role semantics TBD | P0 |
| MON02 | Listen Bus, Solo through Listen, PFL/AFL, Dim | ABSENT | Create isolated signal sink independent MAIN and safe outputs | P1 |
| MON03 | Multiple independent Cue mixes/monitor mix sources | PARTIAL limited monitor sends | True Cue controls, monitoring destinations and memory safety | P1 |
| MON04 | Stereo/mono output modes and pan modes | PARTIAL native Master mono, no true Master PAN | `web/bridge.js` states pan unavailable | P1 |
| MON05 | Low-latency monitor policy, precise buffer/latency readback | PARTIAL | No certified physical 0ms; strict plug-in/PDC budget and FAT | P0 |
| GUI01 | Compact/Narrow/Large console and pinned MAIN | PARTIAL one compact/pinned L2 | role-banked adaptive density and keyed DOM reconciliation | P1 |
| GUI02 | Search, role bank, group-related highlight, copy/paste | PARTIAL visual controls only | Native ChannelId snapshot+selection controller | P1 |
| GUI03 | Performance responsive on standard Windows iGPU | UNQUALIFIED physical | Avoid blur/filter and full DOM mount; issue #56 | P0/P1 |
| GUI04 | Truthful meters, input vs output, clip, LUFS/dBTP | PARTIAL raw PCM/native peak, simulated preview remains separate | No mock LUFS as real native; #135 native analyzer | P1 |
| SHOW01 | Dynamic-channel full Show and portable vendor plugin chunks | PARTIAL fixed 10CH +4 AUX | `NativeShowSchema.h` static; v5 migration by ChannelId | P0/P1 |
| SHOW02 | Scenes scoped Volume/Pan/Mute/Sends/Inserts/Cue/Output | PARTIAL Show operations; full scoped Scene not verified | Native safe-diff + rollback/Recall Safe | P1 |
| SHOW03 | Live FX/Scene smooth recall without unwanted audio gap | UNQUALIFIED | Plugin state and epoch-bound retirement + real hardware pop tests | P0/P1 |
| CTRL01 | Macro knobs, keyboard quick actions, MIDI/OSC surface | ABSENT or limited shortcut UI | Optional mixer controller mapping, not MIDI editing | P2 |
| RELEASE | Real hardware audio and no crackle with 0 VST | BLOCKED | Open #15; 44.1/48k ASIO/WASAPI loopback/soak and audio evidence | P0 |

## Critical corrections to earlier thread reports
1. #243 FAST1 is **MERGED** at this snapshot; not open.
2. #244 M3B dynamic publisher is **MERGED** at this snapshot, **BUT** live callback has NOT switched. Its 16-lane PCM tests are offline/integration foundations, not 16 live GUI channels.
3. A successful Chromium/Windows CI run is **not** physical hardware FAT nor a proof that native Master FX or +BUS GUI is ready.
4. FactoryFX `config/factoryfx-approved-releases.json` retains no approved binary. Sibling code or test artifact != shipped plug-in.
5. The benchmark's DAW-only render/arrange/edit/record features are intentionally excluded. Proprietary Studio Pro Mix Engine FX compatibility not promised; support VST3 and original bus processing instead.

## Official source-to-feature mapping
- CONSOLE, channel types, IN/OUT, copy/paste, visual density, Listen Bus: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm
- Source→Bus→FX, Add Bus for Selected, chain/send drag, feedback, parallel effects, hardware insert: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm
- Focus Editor, macros, inserts, Cue and routing Splitter: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Channel%20Overview%20and%20Editor.htm
- Portable software hardware I/O matrix: https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm
- VCA controllers: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/VCA_Channels.htm
- Scoped Scenes: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Scenes.htm
- Real driver buffer, callback/latency limitations: https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Set_Up_Your_Audio_Device.htm

**Ownership:** #170 product, #192 dynamic graph, #95 FX, #173 processing format, #174 Bus/FX, #175 hardware ports, #179 operator, #180 Scenes, #15 FAT. Do not duplicate related issue or merge conflicting old branch to satisfy this matrix.
