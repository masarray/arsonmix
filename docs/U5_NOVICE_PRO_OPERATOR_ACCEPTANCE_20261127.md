# U5 — Novice-first professional operator acceptance (2026-11-27)

Continuation of canonical [GUI workflow benchmark](GUI_WORKFLOW_BENCHMARK_20261030.md) and PR #953. This is a qualification contract, not a declaration of complete implementation.

## Product precedent
- [Elgato Wave Link 3 initial setup](https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial-Setup): distinct Personal and Stream mixes.
- [Waves LV1 v17](https://www.waves.com/emotion-lv1-v17-news): compact selectable mixer layouts, stable channel controls; [Aux mode](https://www.waves.com/emotion-lv1-tutorial-4-9-mixer-window-aux-mode): sends on existing faders.
- [Yamaha DM3 SENDS ON FADER](https://manual.yamaha.com/pa/mixers/dm3/rm/en-US/6210298251.html) and [pre-fader monitor guidance](https://manual.yamaha.com/pa/mixers/dm3/rm/en-US/6210296331.html): select mix, adjust faders, explicit return; PRE for independent monitor balance.
- [RME TotalMix FX](https://rme-audio.de/totalmix-fx-recording-fx.html): destination-based submix. Borrow mental models, not assets.

## Acceptance scenarios (targets, not results)
| Task from ready mixer | Budget | Observable acceptance |
| --- | --- | --- |
| Unambiguous verified input on fresh mixer | 1 click | No wizard; no patch without user intent; matching native readback; no unexpected level jump |
| Multiple verified inputs | 3 interactions | User chooses actual pin; no guess |
| Add track / open loaded FX / toggle FX | 1 click each | Existing native ownership and status; no duplicate instance |
| Change established MAIN/HEADPHONE editing context | 1 click | Persistent context, no topology write, correct fader/target and return to MAIN |
| Enable saved streaming endpoint | <=2 interactions | No manual global mute/ARM, no primary restart; endpoint identity and installed readback |
| Same-device recovery | automatic when safe | Preserve intent; no silent redirect or emergency-stop override |
| Open saved project | one open action | Restore exact graph and FX or explicit actionable missing-device/plugin status |
| Configure separate headphone mix initially | explicit destination and intent | Warn before silence; starter gain conservative; do not misrepresent POST as independent of MAIN channel faders |

The interaction budget excludes first-time OS/driver installation, ambiguous device identification and physical cabling. Measure click count and time-to-task with at least five novice operators (proposed acceptance sample), using actual Windows builds, 1024x576 and desktop. Record completion, errors, unintended sound or state changes, and assistance required. No usability claim until observed. Expert routing remains under advanced controls; no new wizard, parallel UI graph, hidden mutation, or visual overhaul without evidence.

## Current qualification
Prior head dbef23c: 8/11 workflows passed; Fast UI and AUTO PR0 share G-MV30 source-contract failure caused by new post-projection refresh placement pushing emergency-control sync beyond existing 500-character slice. Keep the original test intact and restore ordering. Anti Noiz P99 headroom remains failing. Current candidate adds one-click single-pin Chromium path, no auto patch, and accurate starter/split PRE/POST text. Full exact CI, actual hardware and usability session still required. Existing starter remains POST; fully independent PRE by default and reversible split/same switching preserving monitor routing require native design and PCM tests, not cosmetic relabeling.

## U5.1 — Native reversible headphone mix (2026-11-27)
- Fresh smart split plans PRE-fader sends from primary direct MAIN branches on the control thread, adds MONITOR and the starter edges in one accepted native publication. A legacy empty mix retains explicit Create headphone mix. Existing monitor sends/FX are never overwritten. A group branch PRE tap is independent of the group fader, not of upstream channel faders. Start at low listening volume.
- Same Mix retains the exact monitor ChannelId, roster, sends and FX ownership; physical MONITOR is unbound via one verified packed native output lease. A deliberately unbound monitor can process its existing graph without a physical copy; unexpected missing buffers continue to reject by default. No second callback, DSP engine, allocation or added primary FIFO. Dormant monitor FX still consume CPU; qualify the load and PDC effect rather than claiming zero cost. Return to aligned timing on Same Mix. Device identity, disarm and callback-observation fences unchanged.
- First Sound uses semantic hidden plus the actual hitbox state, since the premium toolbar uses display:inline-flex!important. DOM changes only on visibility transitions.
- Prior exact head 1d2f7dd: Logical Mixer C++20 passed (new PCM test in dynamic_accepted_session_test.cpp), 399/400 fast native assertions passed; one outdated test explicitly demanded removal of the monitor channel. Revised that test to require the non-destructive native ChannelId path, existing FX lease and default fail-closed missing-monitor checks; new compiled UI fixture covers first split, legacy empty starter, return/re-entry and no implicit topology command for view selection. Latest CI and actual hardware/novice FAT remain mandatory.

## U6 — integration and correlated autonomous output recovery
Baseline 07ef27d: 11/11 pull-request workflows passed, including Anti Noiz CPU (one measured run only), actual Audio I/O Chromium and native PCM. Full MSVC and full Chromium were not run. Canonical #899 advanced to a8b0ad3ac6936251b4610797edcf2865e7e1841c; merge into this development PR only, preserving all canonical paths and the exact two peer SonKu CSS string changes in the shared native bridge. No canonical ref change or release.

Output completion now requires a fresh status, matching ACK revision, accepted exact epoch and graph, callback-observed physical pair and named armed endpoint. ACK/status/graph may arrive in any order; stale graph or hardware cannot release a mode transaction. Recovery retry retains the same wait-for-MAIN condition on timeout or transient error. Tests exercise status-before-graph, ACK-last, wrong revision/endpoint, reversed physical pair, stale epoch and Stop while recovering. No new audio-thread operations or renderer. Full integrated qualification and physical FAT still required.
