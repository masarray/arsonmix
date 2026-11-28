# ArSonMix P12 — Internal acceptance, not release

The artifact is produced only after the exact Windows SHA links, full configured CTest (at least 123 tests), approved GUI verification, independent distribution verification and real candidate installer software FAT. Inspect P12_QUALIFICATION.json and its SHA256 file list. This is NOT proof of physical ASIO or approval to publish. The existing main-only owner dispatcher and historical genuine release rollback gate remain unchanged.

## User hardware FAT
- Export current .smix and back up Studio before installing. Keep existing public installer available for recovery. Use a wired headphone output at low level; select exact ASIO pins, keep MAIN and STREAM disarmed until verified.
- New offline session; saved I/O definitions; reset unassigned I/O; reopen session archive; validate mixer/FX state survives restart. Check that missing/mismatched hardware never auto-patches.
- Test same driver/pins and 48 kHz/256 frames as a starting point; at least 120 seconds dry then 120 seconds actual FX enabled with unchanged graph. Record local AUDIO_HEALTH, audible glitches and exact plugin chain. Repeat with the actual preferred buffer and post-FX STREAM route. Never infer driver XRUN from host timing.
- Test wired 30-minute dry baseline and loaded live vocal/music with Reverb-Sis, Harmony and other intended FX; note dropouts, GUI responsiveness, CPU/RAM and unexpected changes. Perform explicit mute, disconnect/reconnect and verify safe output recovery. Run physical round-trip test only via approved loopback procedure, not theoretical buffer arithmetic.
- Analyze local log with Node 20+: node audio_reliability_profile.mjs audio-diagnostics.log --json profile.json --markdown profile.md. Review device names and private data before sharing. CPU is sampled all-core process utilization; memory trend is not leak proof.
- Record PASS/FAIL/NOT RUN per item, driver model, rate, buffer, exact SHA and listening observations. No auto-arm, automatic driver changes or public release approval.

Active-device New Session remains outside qualified scope; disconnect/mute and verify workspace authority first. Historical cross-release upgrade/rollback and physical hardware FAT remain P13 promotion prerequisites.
