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


## Reconciliation and real DOM A/B timing (27 Nov follow-up)
#996 exact ae9cf49: 3/3 workflows PASS, including actual assembled WIRE + Audio I/O Chromium (46030172587/job 140611918452), fast native and security. Canonical f7bd360 includes #993 and #995; 8/8 scoped combined workflows passed. Common-base WIRE projection fragment byte-identical on canonical; apply only this fragment, append independent source regression, preserve all current peer workflow clauses and add WIRE regression to canonical existing scoped Chromium lane. No DSP/engine changes. Canonical FIELD run 46030631550 active at audit; no duplicate Windows build.

Local headless Chromium 144.0.7559.96 on Linux, isolated actual old/new WIRE projection source copied from GitHub, same 24 strips × 50 inert DOM spans plus mounted controls. 200 warm-up iterations per variant, nine alternating 1000-call batches per variant; median batch mean in microseconds. MutationObserver separate from timed run, live/retained output state parity checked. Not Windows WebView2 or full mixer timing.

| Edges | Mounted | Before µs | After µs | Ratio | aria-label mutations /120 snapshots |
|---:|---:|---:|---:|---:|---:|
| 12 | 0 | 5.0 | 0.2 | 25.0 | 0 → 0 |
| 48 | 12 | 144.1 | 13.0 | 11.08 | 1440 → 0 |
| 96 | 24 | 500.2 | 25.5 | 19.62 | 2880 → 0 |

Reproduce: from a clone containing baseline d0d40ac, install Python Playwright and Chromium, then `python scripts/profile_wire_readback.py`. Optional CHROMIUM_EXECUTABLE overrides executable. No wall-clock pass threshold on shared CI; correctness and operation-count gates remain deterministic. Raw medians from one local environment only; no system CPU/audio speedup claim. Peer updater still active; #994 ribbon regression remains separate. Physical exact-source profiling, full qualification and release NOT RUN.
