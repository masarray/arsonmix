# Handoff v3 — implement true Studio-style ArSonMix Console
Date: 31 October 2026 · Engineering execution / physical FAT

## Product directive and read-first order
Read AGENTS.md, PRD_STUDIO_CONSOLE_DYNAMIC_LIVE_V3_20261031.md, ARD_STUDIO_CONSOLE_DYNAMIC_LIVE_V3_20261031.md, DYN1B_LEGACY_PARITY_BRIDGE_20261031.md, then latest main branch, PR ledger, exact source and CI.
The app is a flexible realtime mixer for karaoke/live/podcast/broadcast; mirror Studio One CONSOLE operator actions (not arrangement/timeline editing), keep original ArSonMix material beauty, optimize for low-spec PCs and do not assume OBS/virtual cable.
Official sources: https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm ; https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/Effects_Signal_Routing.htm

## Accurate baseline and concurrent owners
At original audit main 22e4fdf: M2B #235 merged with 10CH capacity and BUS borrowing an unused strip; DEVICE4 named ASIO/WASAPI source matrix merged; CH1 special legacy VST and native 10CH+4 AUX renderer unchanged. DYN0 and DYN1B are offline; DO NOT pretend live ADD TRACK. #192 = ChannelId graph owner, #95 native FX/PDC, #170 product, #174 bus, #15 hardware 0FX crackle. Older unmerged #115/#117/#225/#229 PRs may conflict; do not bulk merge.
This PR adds a real CONTROL-THREAD group/parallel-FX batch operation plus one deterministic PCM regression. It does NOT expose new live channels yet.

## M3 — one meaningful native live feature milestone, not tiny UX PRs
- Keep exactly ONE callback and graph. Start by compiling actual incumbent routing (10 CH, 4 AUX, monitor, MAIN, source aliases, device/epoch/revision) into ChannelId-based PCM and comparing native sampled audio plus plugin order. Freeze Show compatibility and plugin instance lifetimes; CH1 vendor Orbit and editor must not be orphaned.
- New control API supports ADD BUS FOR SELECTED CHANNELS: unique source IDs, new GROUP ID, source exclusive OUT to Group, Group OUT MAIN, preserve existing sends. ADD FX CHANNEL: same new node but sources keep their OUT and gain-controlled parallel sends. OUT NONE is silent on primary but independent sends can continue.
- Add real native command/ACK with stale revision, cycles, device epoch, full budget and malformed input rejection; prepare VSTs off RT, bounded ramp handoff and memory retirement safe. No new physical pins, duplicate graph, callback allocations or uncontrolled structural change while output armed.
- Gate: two physical inputs to genuinely new Group + FX Channel (not CH10), correct FX insert/process and meter, positive native ACK, rollback preserving old audio and old Show, old 10CH golden PCM, build + package green. No fake GUI +BUTTON until this passes.

## M4 — integrated premium Studio Console GUI
- Full Ctrl/Shift multi-select and right-click Add Bus for Selected Channels in one action. Empty ADD CHANNEL, ADD BUS, ADD FX CHANNEL; rename/duplicate/delete/reorder and saved user Strip/FX state. Safe 1366px banked UI and pinned MAIN.
- Every common strip shows IN verified alias (or BUS IN source count), OUT primary MAIN/BUS/NONE, parallel sends PRE/POST levels. BUS input count opens source list with Select All and Hide/Show Sources; select a BUS highlights its sources and destinations; optional Signal Flow selected map solid primary vs dashed parallel. Show live accepted state distinct from DRAFT.
- No tooltip clutter, blur/glow or huge cards; preserve exact fader PNG, knob feel, 11-strip L2 grid and fast native/WebView2 pointer. Do not re-run routing graph based on hover or per-buffer meter paints.

## M5 — hardening, PDC, true physical FAT and distribution
- Equal CH1..N/FX/MAIN insert lease and editor, native MASTER FX/PDC policy with intentional zero-FX fast path, full VST3 vendor chunks on Show Save/Recall, correct physical and monitor buses.
- Native golden/negative tests (2 mic submix, parallel FX, OUT NONE, nested bus/no feedback, resource budget, stale device, editor lease, plugin failure rollback), Chromium 1366/1440/1680, Windows exact-SHA MSVC/CTest + integrity-verified ZIP, then physical Windows ASIO/WASAPI 44.1/48/96k at 64/128/256 frames, measured latency/xruns, 30-minute soak, #15 0FX crackle, vendor driver unplug/hotplug and low-end iGPU.
- Save exact source SHA, device driver, patch, buffer, log/spectrogram/loopback and evidence. Hardware FAT cannot be marked PASS from CI screenshots. Do NOT promise absolute 0ms latency, unbounded active channels or third-party plugin licenses.

## Merge and efficiency rules
One integrated M3 code gate per meaningful delivery; no repeated commits that run Windows CI for docs-only changes. Refresh main and PR head before merging. Never destroy M2B/DEVICE4/FX lease; reject unsafe operation and retain previous accepted graph. Keep product decision, ADR/PRD/HANDOFF synchronized through versioned docs, not contradictory stale historical handoffs.
