# ArSonMiX GUI v4 — Premium compact realtime Console (Studio One workflow equivalence)
Date: 2026-10-31. **Detailed interaction specification, not a claim of current implementation.** Benchmark: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm and https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Channel%20Overview%20and%20Editor.htm

## Product visual principles
- **Mixer is home screen**. No DAW arranger, waveform timeline, MIDI editor, recording transport, or OBS-specific dashboard. Physical/virtual destination is chosen by operator.
- Retain recognizable ArSonMiX brushed gunmetal, machined smooth recessed fader slots, blue/teal restrained guides, proper fixed green/amber/red meter segment positions, pinned MASTER, current fader PNG and premium rail proportions. Do not replace with giant Material cards/heavy fonts.
- Fast before fancy: WebView2 native UI must be responsive on Intel iGPU and ordinary Windows 11 PCs. No CSS blur, backdrop-filter, large dynamic box-shadow, frame-by-frame gradient/brightness/filters, rerender whole mixer on meter tick or pointer hover.
- Screen density is adjustable: **COMPACT** default, **EXPANDED** selected-only inspector and optional **NARROW** strip mode; preserve readable controls and keyboard focus. Colors label identity, never substitute for text or native hardware readback. No fake animated signals.
- Stage status triad: \`LIVE\` only when native engine ACK matches epoch/revision, \`DRAFT\` is uncommitted, \`BLOCKED\` has explicit reason and safe recovery. Represent output mute as safety state, not hidden color.

## 1366×768 primary layout — logical wireframe (not pixels of final render)
\`\`\`text
╭───────────────────────── ArSonMiX LIVE CONSOLE ─────────────────────────────────────────────────────────╮
│ DEVICE: ASIO / MAONO 48k · 128s  INPUT PCM ●  ROUTE ●  OUT ARMED ●  CPU 18%  [PANIC MUTE] [AUDIO SETUP] │
├──────────────┬────────────────────────────────────────────────────────────────────────────┬─────────────┤
│ CHANNELS     │ MIX | FX RACK | PATCH        [Compact ▾] [Show I/O] [Show Sends] [Sends on Faders]      │
│ Search...    ├────────────────────────────────────────────────────────────────────────────┤  MASTER     │
│ FILTER:      │ CH01        CH02       CH03         BUS01       FX01       CH04 ...             │  MAIN OUT    │
│ All / Input  │ Lead Voc    MIC2       Music        VOCAL BUS   HALL FX                          │ IN: SUM     │
│ Bus / FX     │ IN MIC1     IN MIC2    IN USB L/R    IN 2 src    IN 3 sends                       │ OUT: HW1/2  │
│ VCA / Out    │ OUT Vocal   OUT Vocal  OUT MAIN      OUT MAIN    OUT MAIN                          │ [FX 1..N]   │
│              │ SEND Hall   SEND Hall  SEND Hall    SEND Hall   ...                               │ [METER]     │
│ + CHANNEL    │ [FX 1..N]   [FX 1..N]  [FX 1..N]    [FX 1..N]   [FX 1..N]                        │ [FADER]     │
│ + BUS        │ [METER]     [METER]    [METER]      [METER]     [METER]                           │ MUTE MONO   │
│ + FX CHANNEL │ [FADER]     [FADER]    [FADER]      [FADER]     [FADER]                           │ LISTEN OUT  │
│ + VCA        │ MUTE SOLO   MUTE SOLO MUTE SOLO     MUTE SOLO   MUTE SOLO                          │             │
│ + CUE        │ 1            2          3            11          12                             │             │
├──────────────┴────────────────────────────────────────────────────────────────────────────┴─────────────┤
│ CHANNEL FOCUS · selected: CH01  [SOURCE] → [TRIM / HPF] → [INSERTS] → [PRE / POST SENDS] → [OUT] [CUE] │
│ plugin browser | inserts / sends / macros / routing view  (dock / overlay, not full DOM remount)        │
├────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ Show: Karaoke A ●  Scene: Verse 01        CHANGES: SAVED       [STORE] [RECALL SAFE] [COPY REPORT]   │
╰────────────────────────────────────────────────────────────────────────────────────────────────────────╯
\`\`\`
Wireframe represents intended roles, not real currently available buses, PFL, Macro, CPU or Master FX. Never show unimplemented controls as LIVE. If window width is smaller: horizontal scroll of channel bank only, pinned Master and device safety header remain; one focused channel in narrow mode.

## Workspace hierarchy / panels
1. **Top 34–42px global status**: driver/endpoint, buffer/rate, physical PCM evidence, native accepted route, output ARM/MUTE, measured CPU/deadline and crackle counter; \`PANIC MUTE\` right-aligned and always operable. Device changes in same AUDIO SETUP surface, never a second DeviceManager.
2. **Left narrow rail**: channel search; role filter + visibility checkbox + groups; primary ADD controls; SCENES/FX browser/utility. Preserve existing rail size; collapse secondary utilities into contextual menus. No font above normal UI heading just to advertise a feature.
3. **Mixer viewport**: one stable keyed StripView per ChannelId, role-specific semantics but same controls/layout/geometry, selected outline, independent GROUP/FX/VCA banks. MASTER pinned right; hardware sub-outs as an optional right bank. No permanent spaghetti wires across faders.
4. **Strip anatomy** top-to-bottom: channel number+name/color and role icon; \`IN\` source/verified pin or BUS fan-in number; \`OUT\` primary destination; \`SEND\` small badge/count/open editor; inserts (6 visual rows default with scroll/expand to all supported 8+ actual); trim/polarity/HPF if native; pan/format; MUTE/SOLO/Listen; native meter and physical meter scale; long fader; identity footer. MASTER does not show fake physical INPUT or disabled-looking live FX when actually implemented.
5. **Channel Focus**: anchored overlay or bottom drawer of modest height, not a permanent large pane. Same accepted snapshot, four tabs \`CHANNEL\`, \`ROUTING\`, \`SENDS/CUE\`, \`MACROS\`; no project arranger. Overlay does not cause drag-fader layout jump. Vendor VST3 editor is independent native window with verified lease and can remain open as operator switches channels.
6. **Patch / Audio Setup**: DRIVER/DEVICE selector first, INPUTS/OUTPUTS software matrix with named mono/stereo rows vs verified hardware columns M/L/R, output busses to physical pins, explicit Apply/ACK. After hardware switch, stale port grid hidden until new inventory readback. Preserve DEVICE4 aliases, #243 First Sound.

## Explicit interaction contracts
| Gesture | Operator sees | Native command / feedback |
| --- | --- | --- |
| Select any CH | Strip stays mounted; focus updates with true source/FX/send | Read accepted ChannelId snapshot; no graph/audio change |
| Ctrl/Shift select CH1/2/3 then context Add Bus for Selected | New Vocal Bus strip appears **only on accepted ACK**, sources OUT→Vocal Bus and fan-in 3 | One atomic \`CreateBusForSelected(ids, expectedRev)\`, new stable ID, compile/commit or safe rejection |
| Click IN label | Fast source picker for logical verified source, inline search; no modal wilderness | Existing source controller with epoch and input mask proof; LIVE only on native accepted route |
| Click OUT label | List MAIN, legal BUS, Sub Out, NONE; invalid choices disabled with reasons | Atomic primary-out replacement; send edges untouched; strict feedback check |
| Click SEND + or drag VST3 to SEND | Existing FX/Bus choices OR create new FX Return with inserted plugin; pre/post/pan/level | One candidate with new return/InsertId/SendId; no output until ACK; existing dry OUT kept |
| Select BUS sender badge | Popover Select All / Hide Sources / Show Sources; highlights accepted senders | Read canonical graph fan-in; bank/visibility update does not change audio |
| Move normal fader | Smooth continuous level with numeric dB and physical meter independent | Coalesced RT target with sample ramp and accepted gain ACK, no restart |
| Toggle Sends-on-Faders | Existing hardware MAIN fader preserved; temporary AUX Send controls appear | Continuous existing POST send gain; structural send create/Pre/Pan still guarded |
| Right-click insert | Add, replace, bypass, copy/paste, move, drag to other channel, edit | Real InsertId lease; no copy of VST pointer across lanes |
| Solo / Listen | Explicit solo-in-place vs Listen Bus; MONITOR destination label clear | Listen graph preserves public MAIN, gate output safety |
| Switch Scene | Preview change diff, scope flags and Recall Safe, then apply; no device ARM | Staged validated native scene transaction, rollback on failure |
| Change sample rate/endpoint | Clear name and requires disarm, verified pending device, no UI false LIVE | Device epoch invalidation, device setup ACK, route revalidation, explicit ARM |

## Channel role details
- **INPUT**: actual source/format/trim/HPF/pan/VST/fader/send/out and real input signal. Mono vs stereo independent of whether the user selected hardware L/R alias.
- **GROUP**: IN count (not fake MIC port), sum meter, independent FX/fader, OUT and SEND. All source faders still active. Group remove dialog shows reassignment to previous primary OUT or MAIN.
- **FX RETURN**: fan-in from parallel SEND, VST inserts, fader, target OUT, optional send to another legal FX/Bus without cycles; avoid direct dry route duplicate.
- **VCA**: no PCM signal meter and no VST insert; controls member levels as independent control plane; show member count, mute/solo membership policy.
- **MAIN / SUB / LISTEN / CUE**: hardware readback and own real metering, output mute, mono check, optional FX only when engine supports it; independent PFL/AFL listening should never alter MAIN.
- Unavailable/unpatched channel: \`NO SOURCE\`, silent native meter, insert editor disabled with reason—not preset/mock green LED.

## Context menus and keyboard affordances
Click strip selects; Ctrl click toggles; Shift click range (logical bank display order); right-click context: Add Channel, Add Bus, Add Bus for Selected, Add FX Channel, Add VCA, Rename, Duplicate (independent FX leases), Delete, Copy Channel Settings, Paste to Selected, Hide Sources, Move to Bank and Read Routing. Ctrl+C/V channel state (not browser page), Delete with safe prompt, F3 Mixer/Console, Esc dismiss modal, Space only when specifically assigned and never mutes/arms inadvertently. Document keyboard focus and screen-reader labels for meter/clip/reset. Use disabled tooltip with reason instead of inert click.

## Performance, adaptive density and accessibility
- At 1366×768: pinned MASTER and at least 6 visible compact strips plus scroll/keyboard; avoid huge panels, unused whitespace and oversized font. On 1920×1080 increase visible channels rather than inflating buttons. Maintain horizontal viewport and stable fader scale geometry.
- GUI layout should be data-driven CSS variables for strip width, rail width, utility height; role strip same DOM; fx/meter updates reconcile keyed stable instances without \`innerHTML\` replacement. Normal WebView runtime must not use browser mock timer for LUFS/peak readback. Static decorative shadows only with benchmark; never pointer-follow glow/blur.
- Throttle native status and per-meter updates to display cadence; no per-sample UI messages or CPU-heavy CANVAS shader; single pixel transforms for fader and meter. Tune 30Hz UI telemetry with measured CPU; leave audio thread entirely independent from UI frame pacing. Visibility collapse must not delete DSP channel.
- Test via screenshot diff and actual pointer telemetry on low-end Windows iGPU, Chromium/Firefox web preview (no false DSP promises), native WebView2, 100-channel mocked UI **layout only** with real high-count C++ PCM gate separately. Verify keyboard, contrast, meter color fixed thresholds, clipping latch and role labels.

## Required design acceptance screenshots (when implementing)
A. Compact Console 1366×768 with Mic1/Mic2/Music/Group/FX/Main, no fake activated plugin.
B. Selected MIC 1 focus: verified IN, VST inserts, POST send, OUT Vocal Bus, accepted timestamp.
C. Ctrl+Select 3 inputs → one new Bus with fan-in count and native ACK, error rollback when invalid cycle.
D. Studio I/O matrix real named mono/stereo inputs + output M/L/R with no guessed pins.
E. PFL/CUE isolated from MAIN and safe secondary route.
F. 1366×768 slow-iGPU pointer drag capture with no layout thrash.

## Code ownership
Current files \`web/app.js\` (legacy fixed demo \`channels\` + whole \`renderMixer\`), \`web/mixer-layout.js\` (rail layout), \`web/mixer-io.js\` (device/selected channel/projection), \`web/monitor.js\` (ONE native route controller), \`web/bridge.js\` (native FX identity), \`web/styles-src/*\` and \`web/styles.css\` built. Keep standalone \`AR-MIX-Live-FX-Host-Standalone-v0.3.html\` byte-parity with generated modular script and CSS. Do not copy stale full HTML into current main.
