# P1D reconciliation: real pre-fader source taps and DAG connectivity (2026-10-28)

Latest baseline: `main@d30cf35dc74991286547765ec9927e356273785b`, including the concurrently merged P1D `PreparedMixerBlock` offline DSP executor and the PR #101 lean Windows gate. This patch is the *non-overlapping corrective subset* of the former routing integration PR #112. Never merge PR #112 wholesale: its old CMake/Windows list would drop P1D's `ARMixPreparedMixerBlockTests` from latest main.

## Source and MASTER must have separate eligibility
The original P1A `Plan::extract` intentionally returns silence when `p.active=false`, which includes `faderLinear=0` and mute/solo exclusion. But a **pre-fader** send or an FX input must be able to read the physical source even when the post-fader MASTER contribution is off.

- New `Plan::extractSource` reads **only actual, validated physical input pointers** in preallocated stereo scratch independent of fader/mute/solo state, fails closed to zero for invalid or disconnected physical sources. `ChannelRoutingPlan::extractSource` additionally honors configured stereo→mono format folding. No fake audio or invented physical channel.
- Existing `extract` and `accumulate` preserve legacy post-fader/mute/solo/master output gating. This avoids changing existing P1A/B runtime semantics.
- `PreparedMixerBlock::process` in P1D was already designed to sum raw hardware directly into preallocated buffers *before* fader, so its working fader-zero/pre-send behavior and sample ordering are untouched. This additive API helps forthcoming live source/FX adapters follow the same correct boundary.

## Prepared send graph: rank is not reachability
`ChannelSendPlan::upstreamOf(a,b)` formerly returned true merely when topological rank(a) preceded rank(b), even for disconnected subgraphs. Its name must mean **a real directed path**. At off-RT `compile()` time, compute a 10-bit descendant mask for each node in reverse topological order, using direct outgoing edges plus each child's already-compiled descendants. `upstreamOf` becomes constant-time, bounded, no dynamic memory, no RT graph walk. Cycle rejection remains the existing Kahn gate.

## Tests and safety
- Release CTest `TenChannelAcyclicPrePostSends` now checks fader=0 RAW input→PRE send while post-fader/MASTER remains zero, raw muted-source extraction, stereo-input→mono-fader-zero fold, multi-hop DAG reachability and negative ordered-but-disconnected pairs.
- Existing P1D `TenChannelEndToEndPreparedAudioRender` test already confirms actual offline 10-strip audio processing, pre/post sends and zero-fader behavior; preserve it, its CMake target and main's Windows build target list **unchanged**.
- This patch changes NO `src/Main.cpp`, `src/LiveAudioEngine.h`, `PreparedMixerBlock.h`, JUCE callback, VST3 engine, FX Orbit, meter CSS/JS, Windows CI paths, packaging or active audio.
- P1D remains device-independent execution; **CH2–10 live audio, real device identity, graph epoch/ACK, per-strip VST3 negotiation and #15 clean hardware qualification remain TODO**. No claim of native DAW audio activation or hardware FPS improvements from CI.

Consolidation lineage: original stacked #108 → #110 → #111 was integrated by P1D before this correction. The unrelated PR #112 should be closed as superseded, never force-merged after P1D. `main` latest source-of-truth wins.
