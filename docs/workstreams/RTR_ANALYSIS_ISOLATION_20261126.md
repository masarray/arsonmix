# RTR-1/2 — master analysis isolation and native attribution

Base canonical #899 6970a54e203445330ff7754f40bd049e8be89e06; main 2253001926e6ef318eb00c9673417dc1177df4d6. Independent from #930; shared src/Main.cpp hunks must be reconciled, not wholesale replaced. #837/#927 governing constraints.

## Implemented slice
- Move exact existing 48k K-weight, gated LUFS and 4x FIR true-peak computation from realtime callback to dedicated normal-priority analysis worker. Both existing measurement call sites publish bounded PCM copies instead; no primary playback FIFO, sample shift, plugin or routing changes. Capacity 512 x 256 frames, ~1 MiB fixed PCM storage; at 16 samples 512 slots covers ~171 ms scheduling backlog. No callback wake syscall, mutex or allocation. Worker polls only, lifecycle epochs invalidate old PCM. On queue loss invalidate integrated/TP for epoch; meter stale after 250ms without published frames. No automatic buffer changes or audio restarts. Source remains existing selected measurement tap (logical MAIN before final hardware disarm or diagnostic output); not physical loopback.
- Native per-instance lifetime worst wall time summary uses accepted native insert rows, separately labels legacy original CH1 counters; not additive per-block CPU and does not claim driver xruns.
- Modulo-4 measured render sequence cumulative phase counters batch-published every 64 observations; use same-epoch deltas to investigate the previous near-25% timing pattern. Not exact driver sequence; mixed quantum, skipped renders and epoch transitions may confound phase. Not full flight recorder.
- Separate compact AUDIO_GLITCH_RTR record. Numeric Windows endpoint formatting fixes.
- Add deterministic saturation/order tests, asynchronous meter/reference equivalence and rate/epoch invalidation; periodic phase distribution test. No Windows/physical test claim.

## Boundaries
Unknown third-party FX (including dearVR) remain conservative until exact vendor behavior is qualified. No sleep based on one peak or a name. No PDC changes, no new FX latency. Native graph/FX processing untouched in this slice. Need physical A/B 48k/16/32/64/128, worker overhead/coverage, same scene and callback histogram; zero measured deadline misses only on qualified setups. Meter gap recovery via next device epoch is conservative; explicit analysis-only reset can be added later. No automatic device restart. Prior issue #927 retains full trace, lifecycle and latency work.

## Acceptance
Exact reference meter equality on contiguous PCM; no queue overwrite; data loss invalidates measurement, never audio; epoch separation and no stale output; no changes to actual PCM source or clock. Current canonical release candidate unchanged while qualification proceeds.

## Follow-up hot path pass
Settled unity input trim, wire-sum gain, MUTE/SOLO gate and stereo fader no longer multiply every sample. Settled non-unity uses constant coefficients; active 64-sample ramps retain exact sample order. No FX/tail/PDC skips. Regression compares scalar helper bitwise to original reference across odd/variable blocks, quiet PCM and signed zero. Worker publication throttled to 40 Hz; gap-discarded analysis frames counted until new generation. Independent-clock path skips unsupported analyzer tap.