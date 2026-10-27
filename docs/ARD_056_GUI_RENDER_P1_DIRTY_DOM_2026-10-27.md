# ARD-056 P1: dirty LED-paint and idle telemetry DOM writes

**Base:** `main@0dc201c556623a55348abbb93d8dbd0d1cf08cb4` (Orbit PR #94 merged). **Issue:** [#56](https://github.com/masarray/arsonmix/issues/56). User confirmed **slight** pointer improvement after PR #93; not a hardware acceptance or measured p95 result.

## Audit: actual source paths (cost remains unmeasured on user's PC)

| Trigger | Current source | Identified avoidable work | Remediation |
| --- | --- | --- | --- |
| Preview meter 40/60ms demo tick and native 12Hz status | `web/app.js::setLedMeterLevel` | Writes CSS `--level` on every changed raw level even if the quantized 7px `--led-lit` clip remains **identical** | Preserve float telemetry in JS cache; only write `--level` and `--led-lit` when the displayed whole-row clip changes; opt-in `meter-raw` reproduces old behavior |
| Native 12Hz status | `web/bridge.js` | Reassigns the same footer/audio labels each status event | Update those text nodes/data attribute only when changed |
| Native 12Hz status | `web/monitor.js::repaint` | Rewrites invariant header/status labels regardless of equality | Idempotent `setTextIfChanged` for static or slow-changing labels |
| FX Orbit (PR #94) | `web/bridge.js` | Animated path uses rAF **only when popover visible**, and normal live Orbit runs in native motion engine | Leave untouched; no competing animation scheduler introduced |
| Meter `clip-path` / static painted shadows | `40-rail-pan.css`, `20-button-180.css` | May cost compositing but no verified profiler trace or proof of dominant cost | Preserve artwork and physical pad; no blind Canvas merge or removal of seven resting/pressed shadows |

## Engineering decision

`--led-lit` is the primary authored meter clip variable. The `--level` fallback is retained, but normal-mode writes it only when the visible 7px LED count changes. Full-precision measured level remains in the `WeakMap` cache for accurate resize reconstruction; DSP peaks, CLIP latch, 1600ms peak-hold, native meter update frequency and all L/R semantics are untouched. `?mixperf=meter-raw` restores **just** the legacy per-raw-level write, so same-machine A/B separates this change from BTN-180/hover experiments. No new timer, Canvas renderer, layer hint, hot-path DOM lookup or CPU/DSP callback changes.

Native status idempotency is a separate non-visual reduction: compare existing text before assigning; do not suppress state transitions, diagnostic panel measurements or audio device indicators.

## Evidence and acceptance

Deterministic functional smoke tests call `setLedMeterLevel(e,1)` then `setLedMeterLevel(e,1.1)`; displayed 7px LED row is identical. In production style attribute must remain unchanged while cache retains raw 1.1; in explicit `meter-raw` mode the style attribute must change. Chromium/Firefox and Windows-native CI plus source/offline HTML JavaScript byte-parity required. A deterministic DOM mutation count proves redundant writes eliminated **only for identical quantized levels**, not GPU performance gain.

Physical benchmark still needed on affected Windows PC: repeat matched 10-second pointer sweeps in Perf Lab **optimized** vs **meter-raw** and optionally **meters-off**, record p95/p99 and >33ms frames, actual compositor paint/gpu/driver, matched DPI/resolution/monitor/RDP. No claim that pointer stutter is resolved or that meter-raw mode is an accurate reproduction of every earlier build. Issue #56 stays OPEN until physical gate.

## Rejected shortcuts

- Directly replace DOM clip bars with Canvas PR #60 absent same-device evidence.
- Degrade native meter update rate or L/R precision.
- Force GPU `will-change` on all LED elements.
- Rewrite Orbit PR #94, entire FX rack or audio callback for this unrelated GUI optimization.
