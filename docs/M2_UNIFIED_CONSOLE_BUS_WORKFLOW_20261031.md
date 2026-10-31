# M2 — Studio-style console FX parity and native BUS staging
2026-10-31 | user-visible implementation on existing native 10-input/4-AUX graph.
Owner: #192 dynamic channels, #95 VST3/FX/PDC. No certified physical results claimed.

## Reference behavior
Official Fender Studio Pro / Studio One console manual:
https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm
https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm
Audio, Bus and FX strips use inserts/sends, subgroup output sums into bus, FX send can preserve dry direct-to-MAIN, nested buses require feedback prevention. Match the operation semantics and accessible actions rather than copying branding/code/assets.

## What this branch actually implements
- One presentation pipeline in web/bridge.js: fxLaneSnapshot, fxSlotMarkup and renderRack generate exactly the same 8-row FX workspace and 6 compact strip slots for existing CH1–10. No second visual CH1 renderer. All displayed loaded/bypass states come from real JUCE CH1 chain or native routed VST3 ACK. Unpatched lanes are disabled and clearly marked, not fake active FX.
- CH1 existing VST3 lease, plugin state/Orbit, existing native bypass/move and Show remain unchanged. Other CHs keep their real routed VST3 controller. This only unifies the UI adapter and preserves audio identity until native instance-owner migration is safe.
- + BUS from selected connected CH stages an otherwise unused native CH as BUS receiver, source direct MAIN off, unity POST send to the BUS, BUS MAIN on. Avoid doubled dry signal. Operator must MUTE outputs, APPLY and wait for positive native routing ACK before it sounds.
- + FX BUS stages a separate BUS receiver with 0.25 linear POST send while source still feeds MAIN; the FX BUS inserts can then host real native VST3. No fake reverb added automatically.
- Real native channel-send DAG validation guards self-feedback, cycles, duplicates and resource limits. The wizard also rejects pending drafts, dirty graph, stale device epoch, output armed, no source and no spare native strip; rollback restores previous source main and exact spare channel state.
- No new callbacks, WebAudio DSP, allocations in processBlock, serial device changes or plugin lifecycle added. Existing 10-input/4-AUX DSP and ROUTING/MUTE/APPLY/ACK remain sole authority.

## Status boundaries
- This is a meaningful console workflow and native routing feature, NOT yet full Studio One parity.
- LIVE channel add/remove and arbitrary named buses remain unimplemented. Current +BUS reuses one of the TEN available native strips and explicitly fails when full. User may use that strip for a Vocal/Drum/FX Bus but cannot create CH11 yet.
- CH1 native vendor FX and routed FX still have distinct lifecycle paths despite identical UI. MASTER insert FX, PDC and end-to-end zero latency are not implemented. ChannelId dynamic/Show migration remains the next big milestone: replace LegacyChannelOneLease and fixed 10CH path with one prepared graph + one FxRack keyed by ChannelId, preserving Orbit/vendor state and native ACK.
- No claim of complete Studio One visual copy, zero device roundtrip delay, hardware approval or solved #15 crackle.

## Validation and physical FAT
1. Exact-head native Windows compile, CTest, packaging and Chromium smoke; targeted JS contracts enforce single view model and route wizard fail-closed behavior.
2. Under DIRECT ASIO/WASAPI with outputs MUTED: physical Mic CH1 -> BUS on unused CH10 -> MAIN. With output armed after accepted ACK, exactly one path should be audible. Independently physical Mic CH3 -> FX BUS on CH9, CH3 direct MAIN remains while CH9 processes real reverb VST3.
3. Channel 1, 3, 9 and 10 six FX rows identical in geometry and operator workflow. No ghost live inserts, source/output stays correct, no dropped plugin editor lease/Show state. Bus routing status must only become live after positive ACK.
4. Reject: output armed, source NONE, prior dirty draft, full 10 native channels, obsolete epoch, feedback cycles, VST add failure; preserve previous audible graph.
5. Hardware #15: 0FX baseline and real wired ASIO/WASAPI low buffer (32/64/128), 44.1/48/96k, 30-minute soak, silence/dropout/crackle and low-spec GUI. Only mark product FAT passed after measured evidence.

## Follow-up architecture gate, no parallel DSP
Only one live native ChannelId-based graph, same native instance lease for CH1..N, FX bus, group and main, topology precompile control-thread, stable ID, full VST3 state, safe de-click handoff and PDC. Do not merge any old 10CH prototype branch into main to simulate dynamic add. Check current main and #232 device-owner PR before changes to JUCE audio graph.
