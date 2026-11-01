# ArSonMix v6 — integration/FAT evidence gate
2026-11-01. Owns integration evidence only. Coordination: #192, ENGINE PR #266, GUI issue #268, hardware blocker #15. Source authority: docs/EXECUTION_LOCK_UNIVERSAL_LIVE_CONSOLE_V6_20261101.md.

## New validation
The offline CLI scripts/console_v6_fat_gate.mjs has three modes:
- node scripts/console_v6_fat_gate.mjs trace NATIVE_EVENTS.json
- node scripts/console_v6_fat_gate.mjs pcm NATIVE_PCM.json
- node scripts/console_v6_fat_gate.mjs release RELEASE_RECORD.json dist
- node --test tests/console_v6_fat_gate_test.mjs

Trace schema: arsonmix.console-v6.acceptance-trace.v1. An initial full accepted native snapshot must precede each serialized intent -> native ACK -> full accepted snapshot. Device epoch and base graph revision bind each intent. Accepted commands cannot use stale epoch/revision, rejected commands cannot mutate graph or output ARM. ChannelId is an exact decimal uint64 string; GROUP/FX_RETURN/MONITOR roles cannot appear when native capability is false. RequestId cannot be reused to apply a command twice. A device-loss fail-closed transition requires its own event capture and is NOT inferred from an ordinary reject.

PCM schema: arsonmix.console-v6.pcm-evidence.v1. Four numeric sample cases: independent_inputs maps distinct IN1->CH7 and IN2->CH2; group_fx_return checks serial GROUP gain, parallel wet-only FX Return, no double-dry and separate GROUP/return mutes; monitor_isolation checks separate physical destinations and MONITOR mute not changing MAIN; physical_mute checks MAIN PCM silence. This checks supplied numerical arrays, NOT their authenticity, plugin vendor identity, VST editor, Show round-trip, real output DAC, or audible cleanliness. The self-tests are synthetic and are not evidence from the actual production callback. ENGINE must export trace/PCM from the genuine production native renderer and include actual device epoch, output pin list, and source SHA; coordinate any wire format changes with both owners in #192. Do not add JS DSP or a second native audio graph.

Release intake requires exact 40-character source SHA, both successful native Windows and Chromium CI rows at exactly that SHA, normal Windows verifier + CTest + Playwright records, actual packaged EXE SHA-256 checked against BUILD_PROVENANCE.json/sourceCommit, and separate ZIP/trace/PCM hashes. The validator does NOT fetch CI run results or ZIP bytes: review Actions links, artifact ZIP hash and WebView2/PE findings independently. Existing scripts/package_native_windows.ps1 and scripts/verify_native_windows_artifact.ps1 remain authoritative for binary packaging. The intake also requires #15 wired 0FX physical run for >=1800 seconds with actual backend/rate/buffer/pins and OUTPUT_ONLY_TONE, CAPTURE_ONLY, DRY_THROUGH reports, full report SHA and human witness. This is an operator evidence record, **not** physical certification; even a complete record returns certified:false and ACOUSTIC_REVIEW_REQUIRED.

## Reconciled audit (not rolling assertions)
| Exact tree SHA | Native CI | Chromium | Artifact | Physical |
| --- | --- | --- | --- | --- |
| bb6dff6b42158ba2db4666e0def54bb89abda4f8 (main baseline) | PASS [41263965810](https://github.com/masarray/arsonmix/actions/runs/41263965810) | PASS [41263965813](https://github.com/masarray/arsonmix/actions/runs/41263965813) | Windows 10CH/4AUX FAT candidate; NOT universal | #15 BLOCKED |
| b911c07260388448ab3bfc33084ea114773fe487 (main docs) | No observed exact-head native run at audit | No observed exact-head UI run at audit | Cannot relabel bb6dff6 EXE as newer SHA | #15 BLOCKED |
| 2bbf9f08b5495c3ad61b7d8d0ec5190177bdb55c (engine PR #266) | PASS [41252951831](https://github.com/masarray/arsonmix/actions/runs/41252951831), prepared graph only | Not combined-tree integration | No production callback cutover | #15 BLOCKED |

## Release stop rules
Do not promote until ENGINE migrates occupied CH1 vendor VST/editor/Orbit, other channels, AUX, MAIN/MONITOR, secondary output and Show to ONE actual callback; real CH11, serial Group, wet FX Return and independent monitor pass native PCM + rollback; GUI reflects ONLY accepted state; one combined exact-head Windows CTest and Chromium+Playwright build and verified Windows GUI artifact pass; physical user+engineer listen to 0FX wired output >=30min without crackle, follow with multiFX/Show/device hotplug and record reports. Do not confuse capture discontinuity or callback timing with render underrun. Never claim 0ms physical latency or sound clean from GitHub CI.
