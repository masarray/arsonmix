# P0 Stage B — runtime hot-path simplification (2026-11-05)

**Parent:** [PR #414](https://github.com/masarray/arsonmix/pull/414) — one integrated single-owner PR; [Issue #56](https://github.com/masarray/arsonmix/issues/56) remains open pending physical Windows benchmarks.

## Actual changes — approved native GUI, not historical standalone

1. **Native message thread:** remove the EXTRA 1 Hz accepted snapshot and EXTRA 1 Hz engine.status() read when the WebView is live. Keep precisely one authoritative 12 Hz native readback stream and continuous meter telemetry. Connected 1 Hz AUDIO_HEALTH uses that tick's pre-existing status; an unready browser still logs independent 1 Hz safety evidence. Preserve hotplug, PATCH, ARM, ACK, proper rejected/stale readback, native VST3 and audio callback isolation.
2. **Native bridge:** cache attached strip meter-fill nodes by accepted local ChannelId. Clear on every structural installGraph(), re-read nodes if detached/changed. Dirty-write transport text, warning, menu actions, hardware details, state label and demo-state attributes only when values differ.
3. **Native meter:** a WeakMap remembers the last actual --lit and --peak-extent CSS values and peak opacity for each live DOM meter. The V51 LED step/gradient mapping, native measured PCM and 850ms held marker with timed release continue computing; identical values no longer mutate style. Resizing re-evaluates actual meter geometry. No animations or CSS override.
4. **Native WIRE pointer:** proximity checks reuse a bounded 120ms socket rectangle while the pointer moves within the same tray, invalidated on resize and scroll. Native WIRE gesture/ACK and the original public Pages source remain unchanged. The test verifies one geometric query for a burst, another after resize.
5. **CSS ownership quality:** 18 original approved source-owned stylesheets remain byte-identical; no extra CSS layer. Stage A had 114 existing cross-owner overlaps; Stage B additionally locks **252 pre-existing same-file selector+property repeats**, rejecting any numerical growth while allowing removals. These are baseline **technical debt**, NOT a claim of zero existing duplication. CSS source modernization must be independent, visual-parity-tested, and never rebase immutable historical V28 evidence blindly.

## Qualification and limitations

CI: source CSS no-new-stacking checks + negative tests; deterministic Pages/native bundle parity; exact CSS V28 checkpoints and full Chromium visual controls; native Chromium meter/WIRE dirty-write functional smoke; Windows JUCE compile/CTest, built executable and real hardware FAT when available.

**Expected mechanical improvement** from the 1Hz dedup: remove one redundant engine status read and full snapshot construction+bridge event per second while subscribed (~1 out of the previous 13/s). Actual CPU, GPU/compositor p95/p99, external-window pointer feel and PCM glitch rates require affected-PC evidence in [Stage A profiling instructions](P0_STAGE_A_APPROVED_GUI_LATENCY_PROFILING_20261105.md). No claimed mouse stutter fix without profiling and FAT.

No second mixer, no JS PCM, no auto-arm, no output rerouting, no fader/portrait meter visual redesign, and no changes in the realtime callback.
