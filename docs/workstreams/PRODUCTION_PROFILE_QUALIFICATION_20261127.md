# Production profiling & qualification — 27 Nov 2026

Baseline canonical `d0d40ace87ce9d122f81b05d768af1f4e6a41cbe`; main `379e867985bb6bab8e07297ba7a8c44925b22b2d`. Owner requests measured bottleneck removal and final subsystem qualification. This is not a public release authorization. Preserve accepted FX freeze and one canonical integration.

## Measured GUI readback workload
Instrumented actual extracted paintNativeMeters WIRE projection, same 48 accepted edges, 12 mounted controls, 120 unchanged snapshots (simulated DOM, not real Chromium/Windows wall-time).
| Operation | Before | After |
|---|---:|---:|
| Document selector calls | 5760 | 120 |
| aria-label writes | 1440 | 0 |
| classList toggle calls | 1440 | 0 |
Live and retained output state parity verified, including fixed primary, editable SEND and labels. Native mapping, actual PCM meter processing, safety and DSP untouched. Query once per snapshot, build map only with mounted controls, no retained DOM reference across ticks. Added deterministic source execution test. Real assembled Chromium/full Windows qualification still required; no measured CPU/FPS/latency reduction claim.

## Qualification baseline and blockers
- #927 historical ecaa7369: Maono ASIO 48k/16, 85,899/343,757 host over-budget (24.99%), worst 1300 us against 333.33 us. dearVR PRO 2 peak 1247 us; causal attribution and driver underrun not proven; no current exact-SHA physical capture. RTR M1–M6 already admitted; do not repeat speculative FX research. Obtain paired actual dry/FX 120 s windows, ETW and loopback as required.
- d0d40ac fast native and scoped product runs green; MSVC full CTest skipped in 46021261314. Historic da57dd7 Windows 126/126 (45800599642) is rollback evidence only. Canonical full visual gate not current.
- #995 exact 0392830 7/7 scoped workflows pass, full app/picker hardware not run.
- #992 latest 76dfd7e updater policy FAIL 46029612034. #993 latest 3bf9514 scoped CI running. #994 10e2334 real ribbon Chromium FAIL 46024818058. Do not merge failing/moving peer branches or bypass gates.
- Final production gate: one integrated exact SHA, full MSVC/CTest, full approved Chromium, session/VST3/PCM/FX/I/O/security/updater/recovery/distribution qualification, then attended physical ASIO, WASAPI, loopback/listening and actual release-pair update rollback. NOT RUN for latest source. No installer build until exact candidate and owner-authorized path. No public release.
