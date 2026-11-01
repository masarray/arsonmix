# ArSonMix — SINGLE EXECUTION AUTHORITY (v6 / 2026-11-01)

**READ FIRST:** [PRD v5](docs/PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md) + [ARD v5](docs/ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md) + [EXECUTION LOCK v6](docs/EXECUTION_LOCK_UNIVERSAL_LIVE_CONSOLE_V6_20261101.md). Workstream handoffs: [ENGINE](docs/workstreams/ENGINE_UNIVERSAL_CONSOLE_V6.md), [GUI](docs/workstreams/GUI_OPERATOR_CONSOLE_V6.md), [INTEGRATION/FAT](docs/workstreams/INTEGRATION_FAT_CONSOLE_V6.md).

## Product target and source truth
ArSonMix is a **REALTIME LIVE DIGITAL MIXER** and native VST3 host (stage, karaoke, podcast/radio, user-chosen stream/hardware), with DAW-grade **Console workflow**, NOT a timeline/editor/recorder. No OBS/VB-Cable/vendor restriction. Operator must patch IN1→CH7, IN2→CH2 directly from strips, process two independent genuine VST3, add native CH11, Group, wet FX Return, user-routed independent Monitor/Main, and save Show. Existing HTML mock or green Chromium is not evidence of real sound.

At verified `main@bb6dff6b`: native [Windows CTest/package #41263965810](https://github.com/masarray/arsonmix/actions/runs/41263965810) and [Chromium #41263965813](https://github.com/masarray/arsonmix/actions/runs/41263965813) PASS, but production is **fixed 10 CH + 4 AUX with occupied CH1 legacy vendor/Orbit path**. [Draft engine #266](https://github.com/masarray/arsonmix/pull/266) has no production callback cutover; CH11/Group/FX Return are NOT live. Acoustic crackle [#15](https://github.com/masarray/arsonmix/issues/15) unresolved. Check GitHub SHA and CI again before making future claims.

## Nonnegotiable audio architecture
- ONE primary JUCE audio callback, ONE native accepted stable-`ChannelId` PCM graph and ONE VST3 lease/retirement owner. No JS/WebAudio audio engine, duplicate plugin-host path, simultaneous legacy and dynamic renderer or unqualified dry-only Group substitution.
- Migrate occupied CH1 vendor state/editor/Orbit, CH2–10 FX, four AUX, sends, MAIN/MONITOR/secondary, Show and verified device pins into unified graph BEFORE removing legacy callback; rollback preserves last-known good. Display strip index is never ChannelId/physical port identity.
- Per-channel verified source, independent processing M/ST, inserts, PRE/POST sends, primary OUT, genuine mono/stereo meters and fader. Group serial exclusive primary output; FX Return parallel wet; Monitor independent output. Reject feedback, alias, phantom adjacent hardware pins, stale epoch, incompatible plugin, incorrect clock and unsafe ARM.
- Controls (fader/mute/pan/existing send gain) are bounded RT parameters, not graph rebuild or global MUTE. Structural edits prepare off callback, then immutable publish/ACK `requestId + deviceEpoch + baseGraphRevision`. Device/clock swaps may require guarded disarm but no auto ARM. Callback has **zero allocation, locks, UI calls, disk, VST scan or OS enumeration**.
- Native accepted snapshot/capabilities is sole GUI truth; PENDING isn't LIVE. Unpatched VST3 is real prepared but SILENT. No fake source, hardware input PCM, FX state or browser meter.

## Parallel file ownership — do not collide
| Lane | Coordination | Primary files |
| --- | --- | --- |
| ENGINE | [#266](https://github.com/masarray/arsonmix/pull/266) + [#192](https://github.com/masarray/arsonmix/issues/192) | `engine/core/**`, JUCE `src/**`, C++ native tests |
| GUI | branch `work/operator-console-v6`, [#268](https://github.com/masarray/arsonmix/issues/268) | `web/**`, UI JS/Playwright tests, standalone bundle parity |
| INTEGRATION/FAT | [#192](https://github.com/masarray/arsonmix/issues/192) + [#15](https://github.com/masarray/arsonmix/issues/15) | cross-layer test/packaging/real device evidence; no uncoordinated edits to owner files |

**Operator UX:** single compact console; each strip shows direct IN | M/ST | FX rack | SEND | OUT | PAN/MUTE/SOLO | calibrated LED meter | original SVG fader. Pinned MAIN; dynamic role banks after native ACK; one-click native FX window and per-slot bypass. Bulk Patch only advanced. Stable fader geometry, lightweight iGPU CSS, no blur-heavy glow or oversized cards. Studio One/Fender Studio Pro, Adobe Audition, eMotion LV1 are **functional workflow references**, never asset-copy targets.

## CI + files
- One cohesive vertical PR per lane, no microscopic CI churn. Focused tests before one Windows MSVC/CTest+packaging and Chromium+Playwright at promotion. Logs first on red; do not weaken RT allocation/PCM/device/FX assertions for green.
- CSS source is `web/styles-src/**`; `web/styles.css` and `AR-MIX-Live-FX-Host-Standalone-v0.3.html` must be updated by the deterministic generator `scripts/build_ui_css.py`. Preserve correct fader/LED/native bridge; do not hand-edit generated assets alone.
- Release only exact-head source SHA + verified EXE and artifact, with physical 0FX 30min and integrated 2-input/2-plugin/Group/Return/Monitor FAT. #15 remains open until actual acoustic evidence. Do not claim 0ms physical latency.
- Older 2026-10-25–31 R5/L2/G1/G2/BTN-180 or alternate 'LATEST' instructions in Git history are **historical**, not active strategy; asset regression history is preserved at [v0.8 baseline](docs/GUI_BASELINE_LOCK_2026-10-25.md) but v6 remains authoritative. Historical engineering research only (superseded by v5/v6): [Smart Audio ARD](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md) and [implementation blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md), retained for P0 hardware investigation and regression provenance.; use v5 specs and this v6 lock for priorities. Approved visual details may be reused without overriding operator-first function.

**There are no unattended background coding agents merely because a GitHub issue/branch exists. Separate developer sessions must explicitly be started with the linked workstream handoffs.**
