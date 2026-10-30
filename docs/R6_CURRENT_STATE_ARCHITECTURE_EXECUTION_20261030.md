# R6 — Current-state Audit, Architecture and Execution Roadmap
Date 2026-10-30. Status: CANONICAL PLANNING; NOT DELIVERED AUDIO FEATURES.
Source inspected: main@8251bd6e35af9768a35b0df19ad5f111052eade7. Recheck current HEAD, open PRs and CI before each implementation PR.
Precedence: latest README and this R6 audit, L2 #176, ADR R5, PRD/ARD, current source; older AGENTS sections and old issue snapshots are historical evidence.

## 1. Product goal and external operator references
ArSonMix is one Windows-first, lightweight, premium live digital mixer / VST3 host, not a DAW timeline. Adopt operator interaction (not branded pixels or proprietary algorithms) from:
- Waves LV1 channel mono/stereo flip and layer modes: https://www.waves.com/emotion-lv1-tutorial-4-2-mixer-channels-groups-aux
- Waves all-in-one selected channel / bus routing: https://www.waves.com/emotion-lv1-tutorial-4-10-mixer-route-and-channel
- Waves input/internal/output patch and taps: https://www.waves.com/emotion-lv1-tutorial-2-2-using-the-patch-window
- PreSonus Studio One mono/stereo I/O and output matrix: https://support.presonus.com/hc/en-us/articles/115004767826-AudioBox-1818-Create-Headphone-Cue-Mix-for-Studio-One-in-Windows
- PreSonus logical sub-output routing: https://support.presonus.com/hc/en-us/articles/29973763309453-Studio-One-Pro-7-How-do-I-record-the-mix-back-to-an-audio-track-like-in-Pro-Tools
No claim ArSonMix already has Waves 64/80CH, 44/52 buses, SoundGrid, PDC or all Studio One features.

## 2. Real implemented vs missing, with audited evidence
| Area | Implemented in current source | Not implemented / not qualified |
|---|---|---|
| L2 GUI (#176, PR #171) | ONE 7-row CH1–10 + MASTER shared grid; aligned fader/meter/rulers; FX/ROUTE/SENDS left rail, 3 top workspaces MIXER/FX RACK/PATCH. web/mixer-layout.js/css. Chromium run 40976822422 and Windows run 40976822381 SUCCESS for L2 PR HEAD 93d6341 | NO individual MASTER shrink or new top toolbar allowed. Visual/compiled CI is not audio hardware FAT |
| Native mixer | 10 input strips, 4 separate stereo internal FX AUX returns, bounded 32 PRE/POST send edges, one prepared graph, native epoch/revision ACK | No scalable configurable channel graph, typed GROUP/MON/MATRIX/DCA audio family, no actual bus-to-multichannel render patch |
| Input / channel formats | ChannelRoutingContract.h has ChannelFormat followSource/mono/stereo, sparse input pin mapping and 0.5(L+R) stereo fold | NativeDryRouteCommand.h conflates source mode and channel format, forces BUS stereo; MS1 independent format switch not delivered |
| Sends | G3 #168 native AUX editor with PRE/POST, gain, pan, ACK via web/mixer-io.js + monitor.js | ChannelSendPlan.h / AuxBusPlan.h stereo balance coefficients are not format-aware mono pan; GROUP/MON not native typed buses |
| FX | CH1 real multi-insert + Orbit; CH2–10/AUX one routed plugin per lane | src/VstBusLayout.h insists on stereo main buses; no generic mono/sidechain plugin layout, full multi-insert, PDC; web/bridge.js Master FX placeholders are explicitly NOT live |
| Physical out | LiveDryRoutingLab.h validates callbacks and writes MAIN outputs[0]/[1] | No independent MAIN→1/2 and MON→3/4 signal render; physical output patch read-only until #175 |
| Shows / portability | DRY Show routing, plugin catalog, partial session | Full VST3 state+format+bus/port persistence and user-owned plugin vault across PC changes not qualified (#85) |
| Reliability | GUI geometry and package tests, exact-head engineering baseline | Actual audio with ZERO FX crackles on tested device (#15 open), unverified low-spec/iGPU UX |

Older pull requests #128/#117/#115/#113/#60 etc remain open; do not bulk merge or replace new code. Issue #143 CLOSED is historical input/patch UX, not native physical multi-output delivery. Issues #170 umbrella, #173 MS1, #174 MS2, #175 MS3, #95 FX/PDC, #85 portability, #15 hardware reliability, #176 L2 preservation.

## 3. Four separated typed domains; ONE existing DSP graph
DEVICE: stable device-scoped pin IDs, capture/render direction, enabled sparse callback mask, device epoch, clock and profile. Friendly hardware names are UI only.
SOURCE: NONE, PHYSICAL_MONO, PHYSICAL_STEREO (independently chosen, potentially nonadjacent L/R), INTERNAL_BUS, later PLAYBACK. Source format independent of processing channel.
PROCESSING: MixerChannelId + explicit MONO/STEREO ChannelFormat even when source NONE or BUS. Distinct BusId, BusType FX_AUX/GROUP/MONITOR/MAIN/MATRIX/CUE, BusFormat MONO/STEREO. DCA is CONTROL, never a PCM summing bus. FX insertIds stable across moves/reorders.
EDGE: source-to-channel, channel PRE/POST send to bus, bus-to-bus DAG, and separate bus-to-physical OutputPatchPlan; includes conversion, gain/pan/balance, mute/solo policy, output-port ownership, plugin latency and tap semantics. Source can fan out; physical render port output summation/ownership must be explicit and checked.

Mono→stereo is an explicit panned mono projection, NO invented adjacent physical R. Stereo→mono is one measured 0.5*(L+R) fold at declared boundary with anti-phase policy. Native MONO PAN uses equal-power coefficients; stereo uses BALANCE by default, not invisible crossfeed. Bus sends cannot blindly reuse stereo balance for a mono origin. Do not silently drop saved right pin on reversible format flip.
FX mono-only, stereo-only, mono-in/stereo-out and sidechain VST3 must be preflight negotiated or explicitly rejected; current stereo-only adapter is not proof of generality. Prepare immutable graph, buffers, conversion coefficients, plugin state and PDC plan OFF the audio callback. No heap/locks/JSON/VST construction/device enumeration on realtime thread.
Stage DRAFT through ONE native command and device epoch/graph revision/plugin generation, validate all constraints, publish only after muted structural APPLY and positive ACK; rejected/stale transaction must preserve accepted graph and MUTE. Never auto-unmute. Session/Show uses versioned schema and device/pin remap with rollback.

## 4. Meaningful implementation slices and acceptance, in order
MS1 #173 — Independent mono/stereo CHANNEL, source and bus-input receiver. Extend ChannelRoutingContract.h and NativeDryRouteCommand.h legacy codec to v2 independent source format and channelFormat; PreparedMixerBlock/LiveDryRoutingLab correct real sample processing; status accepted format; selected-channel M/L-R badge in existing L2 bank/inspector, PATCH physical format selector separate. Positive/negative ACK + muted APPLY and Show migration. Test mono→stereo without phantom R, stereo→mono one fold and no +6dB, L/R different tones and nonadjacent sparse pins, BUS/NONE mono/stereo, stale epoch, VST incompatibility, 20x mode flip without CH1 FX/Orbit loss. One whole C++/GUI/test package and exact-head Windows ZIP, no fake format button.
MS2 #174 — Format-aware send/bus DSP. Build typed MONO/STEREO GROUP + FX AUX receive path with PRE/POST, format-correct send pan coefficients, topological schedule and PDC fences; shared reverb one instance for two mono mic sends, real AUX return meters/faders/bank. GROUP and MONITOR are distinct roles; no DCA as summing. Refuse unsupported plugin layouts or latency branch, preserve accepted graph on reject. No mock physical MON output.
MS3 #175 — Native physical render OutputPatchPlan and actual bus rendering. 2-output device: MAIN1/2 allowed, MON3/4 unavailable and safely REJECTED; validated 8-output ASIO: independent MAIN→1/2 and MON→3/4 simultaneously, optional mono sub→5. Stable IDs, sparse output callback index/epoch/clock, no double-sum/crossfeed. Only then enable compact actual-port Studio One style OUTPUT M/L/R patch UI and selected source/destination trace.
MS4 #95 + #85 — true all-channel and Master VST3 insert model with persistent stable slot IDs, off-RT layout negotiation, bounded PDC, FX snapshots/transactional full Show and self-contained user-owned private plugin vault. Same public app capabilities for every user; never distribute third-party VST binaries without rights.

Parallel P0 track: #15 zero-FX hardware crackle must be measured via OUTPUT_ONLY, CAPTURE_ONLY and DRY_THROUGH, driver profile, callback/endpoint underrun provenance and independent clock drift. Do not call product ready on CI alone.

## 5. L2 operator UX contract — no visual regression
Top exactly MIXER / FX RACK / PATCH, FX/ROUTE/SENDS toggle on LEFT RAIL, utilities in rail. CH1–10+MASTER share all seven CSS rows and same dynamic fader height at 1680x920, 1440x768, 1366x768, 1024x720 across all modes. Never alter per-strip intrinsic height or shrink MASTER independently. FX slots show only native processing capability; MASTER ADD FX (currently non-native) must be visibly disabled/not LIVE. Selected Channel Focus summarizes separate physical SOURCE, processor format M / L-R, real inserts, PRE/POST sends, logical bus and actual HW output; it may be an inspector without adding new strip row. Selected AUX send grid replaces ten redundant EDIT buttons as operator flow, reuses G3 state. PATCH INPUT / INTERNAL / OUTPUT show only real enabled ports (two inputs = two columns, not six blank columns), nonadjacent L/R when valid, selected-edge trace only, no constant spaghetti.
Use existing premium metal assets and BTN-180; no glows/blur/backdrop filters, full DOM remount on mouse/meter movement, or separate Javascript audio graph. No fake Master DSP/monitor output/FX return channel. User-facing statuses DRAFT/APPLYING/LIVE/REJECTED/OFFLINE must match native ACK; explain missing ports, incompatible VST, feedback or stale epoch in natural terms.

## 6. CI economy and team orchestration
Track A: hardware #15 isolation and diagnostics (may run parallel with MS1); Track B: MS1 core codec/graph/GUI vertical slice; Track C: L2 visual regression harness and truthfulness, no fake audio; Track D: outline MS3 OutputPatchPlan ABI/device-mask tests without a second engine. Sequential integration owner for files shared by tracks.
Targeted local deterministic C++ tests, JS contracts and Playwright first. Only ONE exact-head Windows MSVC/CTest/package plus Chromium CI when code is ready; docs-only changes do not require expensive native rebuild. Require screenshots and geometry check under 1.1px tolerance across 11 strips/4 sizes/3 modes, source/standalone parity, native positive/negative ACK, sample-accurate mono/stereo and bus tests, source SHA and Windows ZIP. Physical FAT is a separate gate.
No old PR bulk merge, no arbitrary channel-count growth, no second callback, no UI-only DONE label, no graph destruction/instantiation on UI navigation, no hidden auto-unmute or silent plugin-state loss.
