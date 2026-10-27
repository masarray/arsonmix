# ARD-056 P2 — Native meter ballistics and GUI visual audit (2026-10-27)

## Basis and boundaries

Screenshot: user Windows 11 Pro native EXE at 1594×928, Channel 1 LED lit, un-routed preview lanes dark, Auto Orbit visible. This screenshot cannot establish actual FPS, GPU utilization, or repaint duration. Base main de4dd81c; PR #97 is simultaneously modifying FX Rack/channel selection/GUI optics, so visual scope is carefully separated.

## Engineering audit

1. Native JUCE src/Main.cpp calls startTimerHz(12) and publishes arMixStatus at 12Hz. The native audio callback remains unchanged.
2. Old web/bridge.js did source release using Math.max(raw,old-5) for each 12Hz status. That makes a stepped release tied to telemetry count, not elapsed time.
3. web/app.js paints fixed-palette 7px quantised LED segments; PR #96 avoids dirty CSS writes. Do not revert that renderer or distort its stationary color bands.
4. Browser demo simulates separate 25Hz meter changes, not native sound; the new ballistics work only through the JUCE native bridge.

## Implemented scheduling and signal motion

- One shared native meter scheduler for live CH1 input and master L/R/aux, with 34ms timer then one requestAnimationFrame, at most ~29.4 scheduled paint frames/sec (actual observed FPS can be less). No per-strip loops or timers and no idle rAF after levels settle.
- Fast attack: exponential rise with 18ms time constant, never overshoots. Gravity release: begin with at least 14 percent-units/second, accelerate +230 percent-units/s² up to 100 percent-units/second terminal rate, never overshoots.
- Time delta bounded to 65ms; after hidden-tab transition cancel timer and RAF and synchronize to latest target; reduced-motion or ?metermotion=direct displays raw native levels without animation.
- DOM Map prunes stale disconnected LED elements when mixer re-renders. Observability: window.__arMixMeterMotionStats for developer diagnostics only, never advertised as GPU proof.
- No modification to JUCE telemetry/audio RT callback, FX Orbit, FX Rack, PAN, faders, LED colors, clip/peak hold, or shadow styles.

## Visual UX audit, screenshot-based

- Strengths: identifiable 10-channel colors and master, coherent dark console palette, metallic fader cap assets, segmented LED scale, clear Orbit trajectory.
- P0 addressed here: stiff stair-step native metering; improve motion perception while preserving stationary LED color bands and bounded updates.
- P1: repeated ADD FX in the un-routed lanes can visually imply live DSP; PR #97 currently owns channel-aware FX/Rack remediation, so do not duplicate or overwrite that concurrent work.
- P1: tiny LUFS/true peak label, channel names, and footer status need selective typography/contrast tests at 1366×768 and 125% DPI, rather than global font/strip resizing.
- P1: Orbit popup obscures several input strips; current anchored workflow is functional, but reconsider side docking or collision avoidance after PR #97 settles (no extra glow/backdrop blur).
- P1: very strong bevels in top toolbar and left-rail are visually heavier than mixer; coordinate with approved BTN-180 recipes and PR #97 before altering. Do not remove mandated seven rest/seven pressed shadows.
- Preserve honest native routing indication: live CH1 and master meter are not proof that the other nine channels or master FX have audio DSP.

## Verification gate

Run deterministic ballistics tests for rise/fall/acceleration/no-overshoot and single scheduler, CSS/standalone parity, Chromium + Firefox GUI test, and Windows native MSVC CTests/package. Reconcile latest main and review PR #97 before merge. Validate on affected standard Windows hardware with direct versus animated mode, input quiet/loud, Orbit open/closed, p95 and GPU profiler. No claim of real end-to-end FPS or audio FAT from CI.
