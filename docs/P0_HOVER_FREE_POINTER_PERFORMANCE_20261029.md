# ArSonMix — P0 zero-passive-hover GUI (2026-10-29)

Baseline: `main@798df72e58b5ce916328134795250a9182bcbcbd` with approved 10CH+MASTER, VST3/Orbit, 7px LEDs and native splash. User reports pointer movement over GUI feels heavy on a Windows standard PC.

- Production pointer crossings on buttons, rail list, rack inserts, pan, fader caps, FX power and MASTER no longer change gradient/blur, border, shadow, filter or opacity. Legacy 28 hover declarations are present ONLY in explicit `?mixperf=baseline` for repeatable same-PC performance diagnostics.
- BTN-180's sole skin owner keeps all seven static shadow layers, stable physical geometry, 1px press, ON semantic color, aria, keyboard focus outline, and real click behavior. FX Rack context uses `:focus-visible` instead of passive hover, and scene delete remains keyboard accessible.
- Affected UI test checks 12 rest-vs-hover computed style snapshots, with existing pressed/selected/keyboard and JS interaction tests retained. Original CSS module build pipeline keeps runtime and standalone exact parity.
- No changes to audio callback, VST3 engine, Orbit, hardware metering, FX drag/order, device setup, any JS pointer handler, or scheduler.

Evidence: CI and WebView2 physical FAT are different. CI verifies computed style and regression, but the specific p95 input-to-paint improvement must be measured on the owner's standard Windows PC using the existing `mixperf` probe. No extra CI workflow added.
