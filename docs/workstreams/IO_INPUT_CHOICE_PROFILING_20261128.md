# I/O input-choice projection profiling — 28 Nov 2026

Base canonical #899 a61749fbcfa3f11a3268528af4c7cf9fd780e740; main 379e867985bb6bab8e07297ba7a8c44925b22b2d. #1004 already merged canonical. No accepted FX, native graph, commands, device policy, or GUI assets altered.

Measured actual production JS selector inputChoices in isolated V8, same 24 channel synthetic fixture, 300 warm-ups, 5 alternating batches of 1200 calls each. Date.now batch timing; median us/call, not Windows WebView2 or end-to-end UI timing.

| Pins | Before us | After us | active reads / 120 calls before -> after |
|---:|---:|---:|---:|
| 2 | 16.67 | 15.00 | 1200 -> 240 |
| 8 | 55.83 | 34.17 | 23040 -> 960 |
| 16 | 135.00 | 57.50 | 92160 -> 1920 |
| 32 | 370.83 | 95.00 | 337920 -> 3840 |
| 64 | 1161.67 | 154.17 | 1167360 -> 7680 |

Root cause: each generated choice calls physicalInputChoice which refilters all physical endpoints and scans the result again. A call-local first-match Map reuses the single snapshot scan; common label construction is shared, single explicit choice remains direct lookup. No cross-snapshot cache; current device state remains authoritative. 5 A/B fixtures identical, 120 additional deterministic parity fixtures identical, new focused regression executed in isolated V8 using assertions. Includes sparse/remapped/retained/M4/custom/reversed/duplicate pin cases. Reproduce on clone: node scripts/profile_input_choices.mjs (baseline commit must be present). Wall-clock comparisons are advisory, deterministic one-scan and parity are correctness gates.

Existing canonical Chromium failure 46077249775/job 140768042953: DEVICE_REBIND_REQUIRED assertion in P0 workspace restore. Not fixed by this change; do not claim full qualification. Peer #1002 and #973 overlap only the existing test file (append independent test), #1003 no overlap. Do not merge until scoped qualification; no full Windows/FAT or release requested. Physical ASIO profiling and actual UI latency/CPU remain NOT RUN.
