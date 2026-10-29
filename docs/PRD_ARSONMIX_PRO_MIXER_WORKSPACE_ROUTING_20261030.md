# ArSonMix — PRD v2.0: Pro Digital Mixer, Channel Modes, Hardware I/O and AUX/BUS

**Date:** 2026-10-30 | **Status:** CANONICAL PRODUCT TARGET (not a claim of completed implementation). 
**Architecture:** [ARD v2.0](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md), [Execution Handoff](EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md).
**Design evidence:** Nine user-provided reference screenshots: LV1 Classic channel modes, rack, faders, Studio One mono/stereo I/O grid, DAW bus sends and focused source/destination wires. Copy the workflow patterns, not proprietary branded pixels.

## 0A. User-evidence and reference UX (normative companion)
The [nine-image LV1/Studio One/FL workflow benchmark](GUI_WORKFLOW_BENCHMARK_20261030.md) describes precisely what to learn from physical-channel banks, Rack/Dyn/EQ/SENDS/ROUTE modes, Channel Focus, ALL overview, Studio One M/L/R logical-to-physical patch, shared FX BUS returns, and selected-edge visualization. Adopt familiar control hierarchy and state, NOT copied artwork or exact 80-channel counts. The [comment-derived VoC acceptance matrix](CUSTOMER_VOICE_REQUIREMENTS_20261030.md) prioritizes flexible ASIO/WASAPI I/O, multi-output buses, shared FX send/return, dynamic channels and field stability, while differentiating current implementation from requests. Both are binding planning inputs, not claims of finished features. For any product milestone, map one VoC ID to one native observable test, UI workflow and exact-head/physical FAT evidence. PR #167 G1/G2 MERGED at main@527e155 but full driver configuration, physical multi-output and hardware FAT are unfinished; issue #15 remains physically unqualified.

## 1. Product outcome
A usable professional digital live mixer with a single low-latency audio graph and clear **INPUT → VST3 FX → PRE/POST SENDS → BUS → MASTER → REAL HARDWARE OUTPUT** on every channel. Today the core supports ten input channels plus four stereo AUX FX returns. Make the right features visible; never replace a working FX Rack with an unrelated ROUTING panel.

**Primary user-facing success:** On a 2-in/2-out device, select CH1=physical Mic1, load and edit CH1 real VST3 chain, send POST to AUX1 reverb, display Main L/R physically mapped to outputs 1/2, commit changes while MUTED, observe native ACK before LIVE, then unmute deliberately. A larger ASIO interface later demonstrates independent bus→physical-output mappings.

## 2. User personas and three key journeys
- **KTV/vocal operator:** choose microphone and music input; add vocal EQ/compression/reverb; adjust fader/PAN/return; save portable preset. No physical port math.
- **Live engineer:** assign mono/stereo hardware ports and name channel strips; manage FX returns, group buses and sends, follow exact path, find no-sound and negative ACK reasons. No unclear overlay maze.
- **Portable studio user:** scan and locally bundle own VST3/presets, reuse on another PC. Device differences trigger safe remap and validation; public distribution never contains private plugins.

Journey A: Open MIX → select real CH1 INPUT source → click FX RACK / insert slot → add actual VST3 → return MIX without loss → set PRE/POST send → output MAIN L/R → APPLY (MUTED) → ACK. 
Journey B: Open SETUP → select ASIO/WASAPI endpoint → patch logical MONO/STEREO source rows to real pin columns with M/L/R → build logical main output → verify state/epoch; no sudden sound.
Journey C: Open ROUTING → select CH1 or bus destination → draw only selected path or click matrix cell → set send level/tap → APPLY → reject feedback/stale devices deterministically without changing live audio.

## 3. Navigation and visibility contract
| View | User's question | Required controls | Prohibited anti-pattern |
|---|---|---|---|
| **MIX** | What's live now? | stable fader, meter, MUTE/SOLO/PAN, selected input/output labels, FX / ROUTE / SENDS toggle in fixed strip section | routing mode destroying insert slots |
| **CHANNEL** | What happens inside selected channel? | INPUT, real VST Rack, sends, bus/output in left-to-right view | fake EQ/DYN audio controls with nonfunctional DSP |
| **FX RACK** | Which plugins and what order? | Add/Replace, Edit, Bypass, Move, Remove, plugin favorite and Orbit when available | one routing-only button replacing eight CH1 vendor inserts |
| **ROUTING** | Where are many connections patched? | physical INPUT matrix; INTERNAL CH/AUX/group; OUTPUT matrix only when native implemented | sending a graphical link but never changing audio |
| **SHOW** | Will saved state actually restore? | native Show Save/Browse/Recall, plugin state compatibility, rollback | optimistic saved state without native readback |
| **SETUP** | Which driver and physical jacks? | native-reported device/rate/buffer, INPUTS and OUTPUTS M/L/R matrix | raw JUCE dialog as only everyday UI |

Default view MIX. FX RACK and ROUTING are separate workspaces; their state does not reset on navigation. The MIX strip may show FX, ROUTE or SENDS content in the **same fixed-height area** with BTN-180 mode buttons, while faders, channel IDs, MUTE and SOLO stay put. Global mode bank like LV1 and optional per-channel mode in selected-channel inspector. Keep real FX chain accessible from FX mode and FX RACK workspace.

## 4. Layout requirements from the supplied images
- Preserve original ArSonMix graphite/metal console styling, channel colors, smooth fader/meter housing, approved BTN-180 materials. Do not clone eMotion branding/graphics.
- CH1 shows real 8-insert chain in expanded rack, six compact visible strip rows where appropriate; CH2–10 and AUX display only actual native one-insert support until upgraded. Any nonfunctional additional slot must be visibly disabled, not fake an Add success.
- ROUTE strip shows e.g. 'MIC 1 MONO | CH 1 | MAIN L/R'; this is logically different from 'physical HW OUT 1/2'. Click opens focused input source or output summary; no need to open a giant matrix for daily changes.
- SENDS shows e.g. 'AUX1 Reverb POST -12 dB' or 'AUX2 Delay PRE -18 dB', plus Add Send. Each send has enable, level, PRE/POST, PAN and destination. FX return has independent meter/fader/mute and plugin insert.
- A big PATCH view groups INPUT / INTERNAL / OUTPUT and can show at most 8 device columns per page, with physical pin names and actual enabled mask. M/L/R cell annotations distinguish MONO/STEREO. Patch cells are flat indicators; BTN-180 is for commands.
- Optional focused cable trace shows 1–3 selected accepted paths (for example MIC1 → CH1 → AUX1 REV → MAIN L/R → HW1/2). Never draw every edge across all faders at 30/60fps.
- Compact screen works at 1366×768, 1440 and 1680 widths. Keyboard and accessible labels; target click area at least ~30px; no blur/backdrop/filter/hover-wide repaint. Plugin names retain proper capitalization.

## 5. Custom Hardware Audio I/O workflow
Two-level SETUP experience, both using ArSonMix GUI:
1. **Device:** choose real native ASIO/WASAPI endpoint, rate, buffer, profile and enabled channels reported by JUCE/backend. JUCE remains the transport engine; old dialog may be fallback/advanced only. Do not invent capabilities or start rebuilding a device because a tab was opened.
2. **Port Patch:** Inputs or Outputs tab, logical MONO/STEREO rows vs actual callback port columns, M/L/R click mapping and clear. Current output MAIN L/R read-only until physical output patch is implemented in native engine. A 2-output interface **does not** expose phantom AUX/MON hardware outputs.

Default mapping may be suggested only on a matching device, but cannot become LIVE without explicit MUTE, APPLY and ACK on a new device. On disconnect/rate/buffer change, invalidate stale pending mappings, preserve user draft for safe review where useful but not commit silently.

## 6. AUX, sends and bus semantics
Four **internal stereo FX AUX returns** now exist (not four separate physical render outputs). CH source → inserts → PRE tap → fader/PAN/MUTE/SOLO → POST tap; source PRE/POST edges feed AUX return. AUX inserts process combined wet signal, AUX fader/mute/main assignment controls return mix. No duplicate MASTER feed when to-MAIN disabled; fan-out and fan-in supported with tested bounds. Preserve 32 combined send-edge limit until benchmarked.

New daily SENDS bank will show fader/tap and return destination like a real digital console. PRE send follows post-FX but not source fader; POST follows source fader. Explicitly document/test channel MUTE/SOLO behavior for both taps. Reject self/cyclic audio edges and unsupported bus return ordering.

**Later bus types:** GROUP (audio subgroup processing), DCA (control only), MON/CUE (independent monitor mix requiring actual output), MATRIX (downstream mixes). Never equate FX AUX return, GROUP and hardware output merely because the GUI labels them 'bus'.

## 7. Live graph correctness and status
- Every strip uses one native device/port registry, one native route graph, one plugin catalog. Changes to channel source/FX/send/output become DRAFT.
- APPLY requires output MUTED and exact current device epoch/revision; native validates all ports, loops, layouts, catalog IDs, bus capacity, output ownership and pending backup, then prepares off real-time callback.
- Only ACK makes LIVE (green). Reject returns reason (red) and leaves old graph intact. No autoplay or auto-unmute, no disappearing VST3 state, no duplicate plugin instance that processes concurrently.
- Direct physical output routing to extra pins requires native graph code and tests, NOT only a new interface selection. A user on a 2-output device must see unsupported options as unavailable.
- 0-FX real-device crackle issue #15 is a separate release blocker; green CI does not mean qualified physical audio.

## 8. Prioritized acceptance table
| ID | Priority | Required user-observable pass |
|---|---|---|
| UX-01 | P0 | FX and ROUTE toggled 20x while CH1 two real VST3 inserts remain in order and editor/bypass/Orbit available |
| UX-02 | P0 | FX RACK and ROUTING separate surfaces; CH2–10 truthful single native routed FX, no fake eight-insert claims |
| UX-03 | P0 | 1366/1440/1680 UI readable, stable faders, no pointer lag/compositor filters |
| IO-01 | P0 | MIX input shows verified MONO/STEREO source, correct M/L/R for 2/8 enabled ports, stale epoch fail |
| IO-02 | P0 | SETUP displays real device and HW OUTPUT identities; MAIN physical port readback, no phantom outputs |
| TX-01 | P0 | DRAFT → native ACK → LIVE; negative ACK leaves old audible graph and output safe |
| BUS-01 | P0 | Four AUX returns with PRE/POST send level/pan visible and editable from MIX and PATCH |
| BUS-02 | P0 | Invalid CH1→CH1 and CH1→CH2→CH1 or disabled AUX rejected before publish |
| RT-01 | P0 | No allocation/lock/scan/new device callback on real-time audio thread |
| OUT-01 | P1 | On suitable 8-output interface, independent MON/AUX physical output buses audible on assigned pairs, MAIN unaffected |
| FX-01 | P1 | CH2–10 multi-insert and phase-aligned PDC when advertised; no copied live plugin state |
| SHOW-01 | P1 | VST state, hardware map, AUX sends and faders truly restore or rollback cleanly across device changes |
| BUS-03 | P2 | GROUP/CUE/MATRIX enabled only after typed bus graph + output capabilities are proven |

## 9. Deliverable milestones
**G1 + G2:** Real user-facing main mixer I/O and toggle, native FX Rack restored, custom hardware selection/port grid and truthful MAIN output; Windows ZIP and UI/ACK regression. 
**G3:** Full AUX PRE/POST workflow from MIX and PATCH, four processed returns, optional focused route trace; audio FAT evidence and Windows ZIP. 
**G4:** Real physical OUTPUT patch with independent monitoring only on capable hardware; tests for 2-output reject and 8-output multi-bus success. 
**G5/G6:** Per-channel vendor multi-insert, PDC, complete stateful Show and real-device quality evidence. No premature "complete" claim.

Implementation modules, sequencing, source ownership, fail-closed state and CI economics: [ARD v2.0](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md). New thread starts from [Execution Handoff](EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md), not stale old PR screenshots.
