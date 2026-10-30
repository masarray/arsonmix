# ArSonMiX — canonical merge reconciliation and release gate

**Reconciled 31 October 2026.** Engineering baseline: `ff173964c3cecd5b0405ab071f7c82339fbf0f75`. Status = verified source/PR/CI review, **not** field-qualified product release.

## 1. Reconciliation result: preserve real progress

- GUI release baseline: approved L2 #176 + GUI-M1 #211 pinned MASTER, authentic I/O port matrix; RAIL3 #216 correct black upper groove, cyan below cap fading dark at foot. Browser and Windows native CI at preceding main `c2620d1d` SUCCESS: Chromium `41139154061`, Windows `41139153978`, publish `41139291965`. No old UI asset may replace these.
- Native physical route: one existing 10-input + 4-AUX JUCE/VST3 live graph, independent MAIN/MONITOR physical render, optional OUTPUT1 secondary user-selected playback endpoint with its own clock conversion. Real device epoch/revision, output MUTE→APPLY→positive ACK and no auto-unmute remain required.
- DYN1B-1 #212: resource-budgeted ChannelId MAIN+MONITOR PCM kernel, **offline**. **DYN1B-2A PR #215 merged as `ff173964c3cecd5b0405ab071f7c82339fbf0f75`**: actual validated legacy 10CH+4 AUX, sparse port order, pan, mono/stereo, AUX PRE/POST, MAIN/MONITOR, master mono and VST slot identity mapping to one-pass dynamic PCM. Old-vs-new 64-frame sample parity, invalid-plan, NaN-monitor and scratch-budget rejection tests; original PR head `94590266` Windows native CTest SUCCESS (workflow `41138262207`). Post-merge main exact-head CI `41140791061` Windows and `41140791021` GUI were **running at reconciliation read**; check final statuses before claiming the combined build qualified.
- First-party FX metadata admission via pinned upstream release scheme #210 is present; no assertion that six binary factory plugins are packaged/qualified for every platform.

**Crucial limitation:** No live DYN +CHANNEL, no full per-channel CH1-parity VST3 rack, no arbitrary bus/fader-bank capacity and no 0ms measured end-to-end latency. Even 0-FX physical crackling issue [#15](https://github.com/masarray/arsonmix/issues/15) remains open.

## 2. PR merge disposition — no historical code silently lost

As of the audit: **18 historical open PRs are divergent from main**, not fast-forward-compatible; a blind merge brings old versions of hundreds of GUI, JUCE and graph files. Preserve each PR/branch and review narrow missing functionality before any cherry-pick. No old PR was bulk-merged or destroyed.

| PR | Recoverable intent | Where it belongs now |
|---|---|---|
| [#128](https://github.com/masarray/arsonmix/pull/128) | Audio Setup / toolbar / snapshots | Current native setup + MS6 #180; compare before port |
| [#117](https://github.com/masarray/arsonmix/pull/117), [#115](https://github.com/masarray/arsonmix/pull/115) | Old prepared AUX/executor alternatives | DYN1B-2B; never add a second runtime engine |
| [#113](https://github.com/masarray/arsonmix/pull/113), [#59](https://github.com/masarray/arsonmix/pull/59) | Older handoffs/merge audit | Historical provenance; this document supersedes the snapshot |
| [#60](https://github.com/masarray/arsonmix/pull/60) | Canvas meter vs DOM A/B | Test on standard-GPU Windows #56 before any implementation |
| [#29](https://github.com/masarray/arsonmix/pull/29), [#26](https://github.com/masarray/arsonmix/pull/26), [#25](https://github.com/masarray/arsonmix/pull/25) | Two-clock capture/output and recovery prototypes | #15 hardware FAT + OUTPUT1 safety, selective salvage only |
| [#10](https://github.com/masarray/arsonmix/pull/10), [#9](https://github.com/masarray/arsonmix/pull/9), [#8](https://github.com/masarray/arsonmix/pull/8) | Graph swap, VST bus handling, portable presets | MS4 #95 and MS6 #180, do not copy old callbacks |
| [#7](https://github.com/masarray/arsonmix/pull/7), [#6](https://github.com/masarray/arsonmix/pull/6), [#5](https://github.com/masarray/arsonmix/pull/5), [#4](https://github.com/masarray/arsonmix/pull/4), [#3](https://github.com/masarray/arsonmix/pull/3), [#1](https://github.com/masarray/arsonmix/pull/1) | First-sound and earlier UI/FX prototypes | Keep Git history; no release-directed bulk merge |

## 3. Accelerated product path — ONE engine, clear owners, big milestones

1. **MS0 / P0 hardware reliability #15 (parallel, never bypass):** reproduce and eliminate zero-plugin audible crackling, buffer/xrun/device-clock problems using 44.1/48k, qualified ASIO and WASAPI physical interfaces, output+capture endpoint combinations, 0/1/3 plugins, hotplug/recovery and repeated stable-run traces. Detect sample dropouts and actual output PCM discontinuities, not CPU time alone.
2. **DYN1B-2B / #192 — one actual live graph cutover:** use tested DYN1B-2A migration plan to replace, NOT supplement, the current live 10CH+4AUX callback graph. Preserve exact VST3 identity/leases, CH1 Orbit/editor, all inserts/AUX, PRE/POST, MAIN+MONITOR, OUTPUT1, Show and gain smoothing. Stage immutable graph off audio thread, validate scratch/FX/pin budgets, MUTE → APPLY → native positive epoch/revision ACK, rollback on error. No extra callback. Only **after** this gate can DYN2 implement real runtime +CHANNEL and virtualized bank UI.
3. **MS4 / #95 — VST parity:** all channels, AUX and MASTER get one real native insert lifecycle (not placeholder FX). Safe FX reorder, format negotiation, plugin-induced latency/PDC and state preservation; no crackle on reconfiguration.
4. **MS5 / #179 + #182 — LV1 operator UX:** selected CHANNEL focus, typed INPUT/GROUP/AUX/MONITOR/MAIN/DCA layers, sends-on-faders, true hardware I/O patch, bank navigation without moving approved L2 faders and meters. Distinguish audio summing GROUP from gain-only DCA.
5. **MS6 / #180 — live Show safety:** scoped Recall, Recall Safe, vendor VST3 state, portable device remap, transactional rollback. No unmute on failed recall.
6. **MS7 / #170 — production qualification:** deterministic Windows package, plugin admission/signature checks, low-end iGPU pointer responsiveness, memory leak/long-run soak, device disconnect/reconnect recovery, stable live callback peak timing and honest release capability matrix.

## 4. Merge/CI protocol that prevents lost work

- Before every merge, read current `main` SHA, latest merged PRs, live PR heads and the owners in AGENTS. **Do not copy an old whole `src/Main.cpp`, `web/app.js`, `AGENTS.md` or compiler config over current main.**
- One coherent branch per milestone, merged only with green **exact-head** CI and fresh base check. Windows native CTest+ZIP for engine/VST changes; Chromium GUI geometry/source-parity for GUI changes. Avoid triggering Windows CI for docs-only edits.
- Keep RAIL3/L2, OUTPUT1/main-monitor separation, stable VST3 ownership, device epoch and graph ACK as permanent regression contracts; never enable fake output ports, extra callbacks, fake Master FX or UI-only channels.
- No audio-callback heap allocation, file/network I/O, locks with unbounded wait, device enumeration, JSON parsing or plugin construction. Preallocate buffers, make immutable graph, use bounded owner-controlled handoff. Do not confuse CSS coalescing/workers with DSP optimization.
- Field release gates are **independent** of GitHub PASS: real hardware physical click/pop trace, ASIO/WASAPI loopback latency, stress/soak on normal Windows machine, and repeatable plugin+hotplug recovery. Aim for **zero avoidable mixer-added latency and low measured end-to-end latency**, not physically impossible generic “0ms”.
- Preserve PR branches, commit IDs and docs as recoverable evidence; close superseded PRs only after their unique functional deltas have been audited or ported.

## 5. External benchmark boundaries

[Waves LV1 mixer layers](https://www.waves.com/emotion-lv1-tutorial-4-2-mixer-channels-groups-aux), [selected channel/route mode](https://www.waves.com/emotion-lv1-tutorial-4-10-mixer-route-and-channel), [LV1 latency routing](https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html), [JUCE callback documentation](https://docs.juce.com/master/classjuce_1_1AudioProcessor.html). Adopt interaction and engineering principles, not proprietary branded pixels, undocumented latency or feature claims.

