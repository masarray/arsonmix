# P0 Stage A — Approved ArSonMix GUI latency measurement (2026-11-05)

**Issue:** https://github.com/masarray/arsonmix/issues/56  
**Status:** instrumented + CI gate; **affected Windows hardware FAT PENDING**.  
**Target:** only the ONE approved /preview/ GUI, including the PNG hardware keys and its exact JUCE WebView2-bundled counterpart. Historical web/app.js performance probes are NOT this product.

## Exact Windows EXE: reproducible operator test

1. Record EXE artifact SHA, source commit, Windows/GPU/driver, screen resolution, DPI scaling, monitor refresh rate and audio state. Avoid publishing private device details.
2. Press **Ctrl+Alt+Shift+F9** to start the opt-in 15-second trace; press again to stop early. No visible overlay, profiler rAF, style override or periodic sampling exists while idle; the audio callback never receives instrumentation.
3. Sweep the pointer over blank mixer space, labels, FX controls, PAN, faders, WIRE sockets and top chrome without clicking; repeat 3 times for each hardware/audio condition.
4. Read **GUI_PERF_JS** and independent **GUI_PERF_NATIVE** entries in the existing settings/diagnostics/audio-diagnostics.log. Native results also appear in browser-console messages if DevTools is available. Browser console exposes ArSonMixPerf.last, ArSonMixPerf.exportJSON(), and ArSonMixPerf.download() for controlled export.
5. For Pages testing only, use /preview/?armixperf=1. Do not use /preview-v2/ or a second UI renderer.
6. Optional concurrent Windows process sampling: run PowerShell scripts/collect_gui_perf_windows.ps1 -Seconds 20 -ExePath <path-to-ARMixConsole.exe> from a terminal. It saves local environment.json and process-timeline.csv for ARMixConsole and msedgewebview2. This measures process CPU/memory only; GPU rendering still needs a separate ETW/DevTools trace.

## What the measurements mean

JS: rAF frame intervals (median/p95/p99/max, % >33.333ms) by pointer region, event-handled-to-next-rAF proxy, optional Long Task API entries, hidden-page transitions and accepted native snapshot JS processing durations.

JUCE: message-thread timer work mean/max, maximum tick gap, status-read mean/max, snapshot construction/emit mean/max, and count of preexisting extra 1Hz snapshot publications.

**NOT measured:** actual OS cursor position latency, GPU raster/composition, external-window FPS, full hardware input-to-photon latency, or physical audio dropout. These require affected-PC Windows Performance Analyzer/ETW and/or Edge WebView2/Chromium Performance trace. Unsupported Long Task API results are null (unknown), not zero.

## Controlled A/B matrix (3 runs per case, same exact EXE SHA)

| Condition | Target |
| --- | --- |
| Audio disconnected, idle sweep | Basic GUI/hover cost |
| Audio connected, silent | Native control/snapshot cost |
| Audio connected, live channel + MASTER meters | Meter paint/telemetry |
| WIRE expanded, sockets-only sweep | Geometry and interactive drag hints |
| FX rack open, row sweep | Hover compositor and CSS |
| Same assembled /preview/ in standalone Chromium | Browser/native differential (not a substitute for real GPU proof) |
| Optional acceleration A/B with fixed geometry | WebView2/GPU/driver hypothesis only |

Capture environment, output JSON/log summary and DevTools/ETW trace under Issue #56. Do not claim a fix from headless tests.

## CSS architecture non-regression

Approved GUI links **18 source-owned CSS modules**. For the hardware button asset, shipped mixer-control-keys.css comes specifically from site/preview-buttons/assets (not the rejected base skin). Static audit identified **114 cross-owner selector+property conflicts** at baseline, now documented in contracts/approved-preview-css-owner-baseline.json.

The check in scripts/check_preview_css_ownership.py and negative test gates reject:
- new conflicting selector+property in another module;
- additional owner for an existing conflict;
- new duplicated CSS links, link-order drift or new late inline style blocks.

Deleting prior duplicated declarations is explicitly allowed. No new emergency CSS override modules are introduced by this milestone. The ownership gate does NOT prove absence of single-file duplicates, semantically equivalent selectors, specificity conflicts or runtime inline styles: retained CSS architecture + computed-style tests remain binding. Stage B/C should remove debt at its actual source owner.

## Qualification / nonclaims

Stage A qualification needs exact SHA browser functional smoke, CSS ownership nonregression, approved Pages/native byte parity, native JUCE Windows build/CTest and the physical trace collection process. Hardware FAT is PENDING until tested by the affected Windows user. Preserve original GUI dimensions/materials, native meter truth, one JUCE audio callback, safe First Sound PATCH/ARM/MUTE, accepted ACK+later readback, physical routing, VST3 and WIRE interactions.
