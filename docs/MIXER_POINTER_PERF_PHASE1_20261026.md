# Issue #56 — Mixer pointer jank, A/B phase 1

**Baseline:** latest main at 8d5f974, after PR #54. This is a source-backed mitigation and an opt-in profiler; no user-hardware trace has been captured, so the cause and speedup on the affected Windows Firefox PC remain **unproven**.

## First implementation
- Remove unnecessary hover contrast and drop-shadow stacks from silver fader caps and extra PAN hover drop-shadow; use simple brightness. Insert hover uses border color; channel-name hover uses subtle outline rather than filtered full element. Pressed/ON visual geometry, original fader artwork, Button 19 and Button 180 unchanged.
- Remove unconditional will-change:transform from all meter-fill elements. Transform animation still works; browser can choose layer promotion.
- Browser-only `?mixperf=` variants: optimized (normal), baseline, hover-off, meters-off, layers-off, all-off. Normal view has **no** diagnostic pointer tracker. The demo meter pause experiment never touches actual native JUCE telemetry.
- Pages lab: /preview/perf.html. Record a 10-second movement session via explicit button. It reports rAF frame interval median/p95, >33ms/50ms counts, pointer moves inside mixer vs toolbar, available Long Tasks, browser and DPI. It is not GPU paint time, mouse-event latency or audio XRUN.
- CSS ownership is web/styles-src/41-mixer-performance.css. CSS/JS must stay in parity with offline standalone. Chromium tests validate mode isolation and unchanged geometry.

## Firefox field experiment
After merging to main and deploying Pages, open https://masarray.github.io/arsonmix/preview/perf.html. For each variant click Record, sweep across FX slots/faders/PAN and toolbar, repeat 2–3 times. Copy JSON and compare p95 frame intervals and >33ms events normalized by pointerMoves. High improvement with hover-off implicates hover paint; meters-off implicates demo meter animation; layers-off implicates layer promotion/memory. Same results in all variants suggests Firefox graphics/driver, extensions or high mouse polling. Compare direct mixer.html vs iframe. Capture a second Firefox Profiler trace at https://profiler.firefox.com/ and about:support Graphics if necessary.

## Constraints
Preserve 13 strips, six FX inserts each, metallic 31×90 fader PNG/no-jump drag, PAN center snap, peak hold/clip latch, ASIO/WASAPI and native meter bridge. Do not touch realtime audio callback or immutable reference theme. Headless CI passing is not evidence of performance improvement on the owner's specific PC.

## Phase 2 — 2026-10-26 browser preview idle-frame suppression
The browser-only synthetic meter loop previously requested a display-rate rAF (60–144Hz), then returned early on nearly every frame because of its 40ms throttle. Now one 40ms timer schedules one rAF for the visual tick; there is no permanently recurring display-rate callback. Timer lateness >25ms across three wakeups degrades to 60ms; 30 healthy wakeups restore 40ms. Visibility change cancels the timer and outstanding frame; foreground schedules exactly one next tick. Native JUCE meter updates bypass this entire demo scheduling path and real audio/DSP remains unchanged. `browserMeterStats` exposes executed tick and late-wakeup counts to Chromium smoke/developer tools. This is a source-level CPU scheduling reduction, **not** hardware FPS proof. Leave issue #56 open for Firefox A/B recordings.
