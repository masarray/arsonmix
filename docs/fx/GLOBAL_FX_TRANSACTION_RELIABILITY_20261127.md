# Global FX transaction reliability — 2026-11-27
Base 6b324282413ace66caa60928f9b24f3a915b33fa. Narrow shared product regression, not reopened accepted FX DSP work.

- Native rack repaint previously wrote every power attribute and title on each projection; native tooltip sanitizer removed titles. Changed to compare-before-write, preserve labels and plugin latency as aria-description, avoid title churn, retain rack/slot/global distinction and exact mounted roster checks.
- Global A/B projection no longer calls the view hook when mounted power state is already correct; busy and disabled writes are conditional.
- Physical floating power previously released its one-in-flight fence on ACK alone although native child snapshots are throttled. Now hold until correlated accepted ACK and matching native readback, in either order; reject mismatched receipt, retire on disconnected/changed device hardware or instance, timeout remains explicit. Durable logical rekey/ACK contract unchanged.
- Actual Chromium regressions: unchanged rack snapshots, physical popup ACK/readback ordering, stale receipt, existing add/move/remove/independent slot and seven original popup editors.
- No audio callback changes, no new DSP, no new renderer, no physical test claim. Verify exact CI before canonical admission; no duplicate FIELD while previous exact candidate builds.