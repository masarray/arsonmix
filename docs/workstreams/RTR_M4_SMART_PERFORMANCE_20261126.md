# M4 — Smart Performance Assistant (RTR; 26 Nov 2026)

## User-visible milestone

Audio I/O → **Performance** on the single owner-approved /preview/ WebView presents a compact, native-evidence-only panel:

- Last complete **one-second** host-render interval: epoch-fenced 13-bin cumulative histogram delta, P99 **bucket ceiling**, conservative deadline margin and count of host render blocks above period.
- Most demanding **observed lifetime FX peak**, explicitly labeled as *historical* and *sampled* when the CH1 external legacy lease uses sparse observations. This is not instantaneous CPU load.
- M2 native-ranked **FX to review** (max three) with actionable, non-authoritative description and last complete 32-invocation mean where valid.
- Declared MAIN/MONITOR graph PDC; aligns with M3 independent monitor policy, explicitly excluding driver / DAC round-trip.
- Last completed 64-pair M3 callback-start context, with neither a causal claim nor inferred physical xrun.
- Advice for measured render over-budget, limited headroom, scheduling suspicion, latent bypass, and largest declared insert latency. Recommendations never command a bypass, alter FX parameters, change ASIO buffer, re-route, auto-arm or allocate an extra audio FIFO.

## Architecture and evidence contract

**No native/DSP code changes.** All input already appears in one existing 12 Hz JUCE `arMixStatus` read; only the event containing `footerHealthSequence` once per second invokes the pure view projection. No JS timer, new native poll, audio-clock read, or worker. The `smartPerformanceView` pure function in `web/approved-native-contract.js` uses same-epoch accepted schema-v6 device identity, exact rate/buffer and monotonic 1 Hz sequence before using cumulative bucket differences. Malformed input, device transitions, counter resets, insufficient samples (fewer than 64), missing native callback, or retained offline session cannot turn into an invented 'green' state. One snapshot is **COLLECTING**, not a signal of success. UI never appends unescaped plugin HTML: FX/plugin names pass into `textContent`. Everything remains on the original approved native Audio I/O DOM; public `/preview/` baseline is not changed or forked.

P99 from 13 inclusive boundaries is a **bucket upper bound**, so '>100%' means top unbounded bucket, not a clipped fake percentile; remaining margin is called 'none verified'. The largest FX peak is the lifetime-normalized per-invocation worst, not total application CPU and not proof an FX caused the audible crackle. The native 32-observation and 64-pair windows may be older than current status and are presented as review/history, not instantaneous proof. The histogram is **host-render budget**, not a driver xrun signal. The status is not a hardware FAT or elimination claim.

## Evidence and integration

Starting point: PR #932 exact head `fddaef25d2c599d7d388cb4c52affe6cbed79e33` (M1–M3), base canonical #899 remains protected and unmodified. M4 is isolated on a branch forked from that head so another thread cannot overwrite the active M1–M3 owner. The dependent PR targets the #932 branch, **not main or the canonical release candidate**. Existing owner-approved GUI/native paths are preserved.

Targeted deterministic native-transport tests are appended to `tests/approved_native_m2_contract.test.cjs` and are included by `ui-smoke.yml` quick gate. They cover missing evidence, consecutive-interval math, pressure, stale epoch / retained state, cumulative counter reset, callback pairing, historical sampled FX labeling, PDC (1456 / 0 frames) and no automatic actions. Exact CI may remain pending; separate Windows binary build and attended ASIO field test required for deployment. No claim of measured crackle elimination or physical latency reduction.

**Next:** M5 integration and real ASIO qualification after in-scope PR and canonical conflicts are reconciled at exact SHA. Preserve no automatic buffer, plugin, monitoring or device changes; physical listening and loopback must decide the real user result.
