# ARD-056: P0 idle-pointer rendering optimization

**Date:** 2026-10-27 | **Base:** `main@78b71bd056f3c0a81d88f0fb1c41ff85d76749ef` | **Issue:** [#56](https://github.com/masarray/arsonmix/issues/56)

## Scope and known facts

The user reports pointer stutter on Windows 11 Pro server hardware in both browser and JUCE/WebView2 native EXE, but smooth results with RTX 4050/3060 Ti. GPU model/driver, DPI, direct/RDP, WebRender/Edge compositor traces and affected-PC p95 are not available. Source inspection finds avoidable *work*, not a confirmed root cause or measured improvement. Preserve native audio callback, VST3 hosting and visual geometry.

## Audit (risk ranking by executable path; GPU paint cost NOT measured)

| Area | Source and trigger | Decision |
| --- | --- | --- |
| Global BTN-180 | `20-button-180.css`: large set of buttons with seven resting shadows; `brightness(1.32)` plus filter (120ms) and shadow (90ms) transitions per pointer hover | Keep seven resting / seven pressed shadows, 3px radius, all palettes and 1px press; remove passive brightness and filter/shadow animations |
| FX, fader PNG, PAN | `41-mixer-performance.css`: normal optimized path already suppresses costly hover filters | Keep normal path, original artwork and relative drag |
| Native pointer edit guard | `bridge.js`: capturing `pointermove` always calls a DOM `closest()` guard even when not dragging | Remove global move listener only; keep pointerdown/up/cancel, wheel, key, active `drag` readback guard, 350ms grace |
| LED bank | `40-rail-pan.css`: fixed colors plus dynamic `clip-path`, 12Hz native / bounded preview timer | No speculative Canvas change; await meters-off physical A/B |
| Legacy filters, overlay | `10-console-legacy.css`: dormant historical blur/filter rules; `41-mixer-performance.css` already overrides common active paths | No broad selector deletion without computed-style proof |
| Status/monitor bridge | `bridge.js`, `monitor.js` native status | Leave intact until main-thread profile justifies further work |

## Decision and alternatives

Change shared web renderer, not DSP. BTN material remains one exact seven-shadow surface for OFF/ON; state changes still animate color 150ms and 1px press translates 75ms. Pressed shadow geometry remains but switches immediately, avoiding interpolated shadow paint. `?mixperf=baseline` restores only historical BTN-180 brightness for same-hardware A/B; this is **not a full historical build**. Normal / `optimized` no longer uses BTN-180 brightness hover. Do not add new compositing layers, framework, animation queue, `!important` cascade patches, or merge unmeasured Canvas PR #60.

## Verification and field acceptance

Static gates: concatenate eight modules to exact runtime CSS, embed exact CSS in offline HTML, button geometry/shadow/palette contract, calculated normal-vs-baseline BTN-180 hover, no idle native pointermove. Automated Chromium and Firefox smoke plus Windows native CI must pass on exact PR head.

Physical test is pending: use `https://masarray.github.io/arsonmix/preview/perf.html` after deployment; 2–3 matched 10-second runs, modes `optimized`, `baseline`, `meters-off`, `all-off`, same cursor route across BTN-180, FX, fader, PAN and outside. Capture median/p95/p99/over33.3ms, browser, GPU driver, DPI/refresh, direct-vs-iframe, Edge/Firefox paint/compositor trace. rAF intervals are **not** input-to-photon latency. Distinguish engineering/CI completion from hardware qualification; Issue #56 remains open until actual affected PC validates improvement.
