# P1A/P1B/P1C reconciliation — compiled routing foundations only (2026-10-28)

## Current main and dependency decision
Integration base: `main@c0d7c794a27fcdc7adbe77c6166a2e97b848b82c`, which includes #106 visually smoothed native LED meters and #107 sample-accurate FX bypass de-click. Existing stacked drafts #108 (P1A), #110 (P1B) and #111 (P1C) were developed against older bases. Original #108 is **merge-dirty** against main after #107 changed the same Windows CMake test/build line. Do **not** force-merge the stale branches or accept replacement of the de-click/Orbit tests.

This integration takes the reviewed source implementations from head #111, which contains exact ancestry #108 → #110 → #111, then fixes a cross-module pre-fader extraction issue identified during integration review. `extractSource` now makes raw physical capture independent of fader gain, mute and solo, while `extract` retains post-fader MASTER behavior. A new Release regression verifies zero-fader pre-fader sends, muted source taps, and stereo-to-mono raw fold. Only CMake `target_sources`, CTest registration, Windows build target list and this handoff were integrated additively from latest main. No new CI workflows; no base file reset; existing `ARMixFxBypassEnvelopeTests` and `ARMixDearVrPolarPolicyTests` must remain built. All three C++ routing test files have `#undef NDEBUG` *before* `#include <cassert>` to keep Release test assertions executing. Previous P1A alone did not have this, so do not revive its raw 2026-10-27 test blob.

## P1A: deterministic source routing (10 input strips + MASTER)
`engine/core/MixerRoutingPlan.h` compiles mono/stereo input maps, gains, panning, mute/solo, and master mono/gain/balance offline. Fails closed for invalid routes and nonfinite gains/pans. RT extract/accumulate/finish use caller-owned preallocated arrays. See `docs/P1A_REAL_MIXER_ROUTING_CORE_20261028.md`.

## P1B: source format is separate from callback index
`engine/core/ChannelRoutingContract.h` supports independent `followSource/mono/stereo` formats, a validated callback-index lookup derived from sparse **physical port identities**, and mono fold / dual mono. It does NOT assign live device indices or drive native GUI dropdowns. See `docs/P1B_MONO_STEREO_PORT_CONTRACT_20261028.md`.

## P1C: static cycle-safe sends
`engine/core/ChannelSendPlan.h` prepares a bounded 32-edge acyclic send graph, deterministic ordering, pre/post-fader taps, send balance, bus-return semantics and `toMaster` flags. No per-pointer-move send commands, no native stage activation. The live-processing caller will be responsible for correct pre/post tap buffers, solo/mute policy and graph-epoch handoff. See `docs/P1C_INTERNAL_SENDS_DAG_20261028.md`.

## Frozen production boundaries
Current live native chain remains CH1 + MASTER. **CH2–10 and MASTER FX are not yet native multi-bus processing.** Source mapping/FX routing GUI controls must remain disabled/truthful until: (1) physical endpoint ID and callback mask are verified; (2) graph compiled and published with epoch/revision ACK outside RT; (3) deterministic no-heap callback and per-plugin PDC verified; (4) #15 zero-plugin hardware crackle qualification; (5) real-device mono/stereo/multi-input and sends FAT. FX reorder #102 remains separately owned and draft; CI optimization #101 is independent and must not replace native test gates.

## Verification
- P1A test: `TenStripStaticRoutingAndMaster`
- P1B test: `MonoStereoSparsePhysicalPortRouting`
- P1C test: `TenChannelAcyclicPrePostSends`
- Full existing native CTest and single Windows GUI artifact workflow on exact integration SHA.
- Chromium/Firefox GUI smoke includes the real MASTER L/R LED regression and latest Orbit.
- Validate newest main HEAD, PR review and complete CI prior to merge. Postmerge main CI and Pages must be green.
- This is a **compile-only engineering milestone**, not a functioning 10-channel DAW. Neither FPS on owner's Windows server nor native multi-I/O hardware audio has been qualified.

## Late reconciliation with PR #101 (2026-10-28)
While integration PR #112 was being validated, another thread merged PR #101 at `main@66989fa46f004aa852207f1159bf3e1b8e9506c1`: path-filtered native PR gates, current-main-only verified Windows FAT artifact, and stale-safe Pages publisher. Its three workflow changes and policy Node tests are now authoritative. PR #112 was reconciled by a two-parent merge commit (no overwriting `main` workflows). Only three native test targets were inserted into **PR #101's** new Windows build list. `tests/ci_workflow_efficiency_test.mjs`, concurrent Pages policy, Chromium/Firefox gate scheduling, mandatory de-click/Orbit CTests, and main-only artifact upload are preserved unchanged. Main CI and full on-head Windows MSVC CTest must re-pass following this integration.

## Send connectivity correctness gate
The integration audit also corrected `ChannelSendPlan::upstreamOf()`: comparing topological rank alone falsely reported a connection between unrelated strips. The compiled plan now stores a bounded per-source 10-bit descendant mask computed in reverse topological order once during off-RT preparation. Query is O(1); Release CTest proves multihop reachability, disconnected-but-ordered channels, self and reverse negatives. No new runtime graph traversal or heap allocation.
