# ArSonMix GUI hot-path audit — 2026-10-30

**Source baseline:** `main@593427d3bcbff9ebbb3ce7e2d0bd3e38333bbbba`. Scope: Chromium/Firefox preview + native JUCE WebView2 rendering; no native DSP graph changes. This is source-path evidence, **not** a claim of measured GPU speedup.

## Findings and changes

| Trigger | Root cause / risk | Implementation or decision |
| --- | --- | --- |
| Mixer subtree MutationObserver | 12Hz native meter/loudness text replacement in MASTER can generate childList mutations, unconditionally scheduling `syncView()->patchMixer()` across 11 strips. | Reconcile only on mixer root replacement, MASTER container redraw or FX rack replacement. Preserve existing microtask coalescing and ACK-driven explicit `repaint()`. |
| Fader readback from native | CH1 legacy, routed channels or MASTER set PNG cap `--pos` but not blue slit `--fader-open`; visible inconsistency with UI-controlled drag. | Set both properties in the existing status handler (no new loop or timer). |
| Native loudness ARIA | `aria-label` assigned on each status tick, even when unchanged. | Compare before writing. |
| Browser UI-only autosave | `setInterval` serializes and writes the entire snapshot envelope every 5 seconds regardless of changes. Potential synchronous long task for large snapshot catalogs; do not remove without full state-dirty coverage and rollback tests. | Follow-up P1 dirty-event + idle-debounce transaction, preserve atomic failed-write behavior. |
| CSS legacy and static materials | Many historical declaration overrides plus inlined font/PNG, gradients and shadows; majority of optimized-mode hover filter effects already suppressed. | Preserve approved BTN-180, fader and LCD skin; avoid unmeasured mass deletion, Canvas rewrite and brushed texture. |
| LED rendering | Shared 7px quantization/cache, bounded 34ms native motion and hidden-tab pause already reduce unnecessary styling. | Preserve audio-native peak truth and meter layout. |

## Acceptance, release gates and field measurement

- Native Chromium smoke: text-only MASTER LUFS mutation must not trigger rack repair, but re-rendering MASTER must.
- Node source contract: only structural mutations trigger rack sync; CH1 legacy, routed lanes and MASTER keep blue slit consistent with native gain readback.
- Existing GUI L2 geometry (#176), CH1 Orbit/VST3, native epoch/revision ACK, 11 strips, source/standalone parity unaffected.
- Run exact-head CI before merge; passing simulated Chromium does not measure low-end Windows PC latency. Use [Perf Lab](https://masarray.github.io/arsonmix/preview/perf.html) and issue [#56](https://github.com/masarray/arsonmix/issues/56): compare optimized, baseline, meters-off on the **same** iGPU machine and cursor trace, 1366×768/1440×820, 100/125/150% DPI, Edge/Firefox/WebView2, direct/RDP, log p50/p95/p99 and long tasks; attach browser profiler if still slow.
- Audio glitch/crackle on actual ASIO/WASAPI with 0FX (#15) is a separate release blocker and cannot be inferred from this GUI source audit.
