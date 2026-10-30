# MS1 Native processing format — real 10-channel mono/stereo vertical slice (2026-10-30)

## Authority and provenance
- Parent [Issue #173](https://github.com/masarray/arsonmix/issues/173) and [ADR R5](ADR_R5_NATIVE_MONO_STEREO_CHANNEL_BUS_ROUTING_20261030.md); [R6 native audit](R6_CURRENT_STATE_ARCHITECTURE_EXECUTION_20261030.md), [operator workflow](R6_OPERATOR_WORKFLOW_CONTRACT_20261030.md), [L2 immutable layout #176](https://github.com/masarray/arsonmix/issues/176).
- Base main `05d6fdc716e39d38ec99e2bfbd4641bf7d4b0b41`. Concurrent [G5A PR #178](https://github.com/masarray/arsonmix/pull/178) is **OPEN / DRAFT**; this slice deliberately does **not** modify its `engine/core/LiveChannelFxGraph.h` or `web/bridge.js` / `web/plugin-ui.*`.
- This is a **code implementation/CI candidate**, not a completed hardware qualification, physical 8×8 output or mono VST bus-layout claim. #15 crackle remains OPEN.

## Complete operator journey
1. On a verified DIRECT ASIO/WASAPI endpoint, ensure OUTPUT MUTED and a real input patched to any CH.
2. In MIX's left rail select ROUTE; CH1–10 have a compact **CH M / CH L/R** processing selector separate from PHYSICAL INPUT selector, using exactly the former LIVE/DRAFT status row (no new fader grid heights). The 10×actual input-port PATCH matrix has source mode NONE/MONO/STEREO/BUS and *separate* processing selector. Both surfaces modify exactly the same controller's draft.
3. Choose physical **MONO microphone input**, select processing **CH L/R**, observe DRAFT. The physical source stays MONO and occupies ONE validated hardware capture pin; no guessed right input. CHANNEL stays STEREO at the processing contract.
4. Click existing APPLY while output muted; controller sends **schemaVersion 2**, per channel explicit `sourceKind`, `sourceFormat`, `channelFormat`, unchanged port assignment and one device `epoch`/`revision` to `routingDryApply`. Native C++ verifies coherent typed fields and compiles **one existing prepared render graph**; rejected changes never update accepted graph. The existing native ACK updates both MIX and PATCH; no separate output arm or vendor reset on tab switch.
5. To test STEREO source→MONO channel: assign two independently enabled L and R pins in PATCH (may be nonadjacent), choose processing CH M. The existing `ChannelRoutingPlan` and `PreparedMixerBlock` fold exactly 0.5(L+R) once before FX; channel runs MONO equal-power pan on L/R accumulation. No phantom pin.
6. NONE and BUS receiver may retain a real declared MONO or STEREO format independent of physical ports. The existing fixed AUX1–4 are still **stereo**, NOT new mono AUX and NOT physical output.
7. Device epoch change invalidates pending physical selection and returns safe unpatched state; stale revision and invalid fields reject without mutation. On negative native ACK, operator may REVERT to last accepted native route.

## Wire policy and backwards migration
- Legacy request missing `schemaVersion` is treated as v1; its modes keep historical behavior (NONE default mono, PHYSICAL MONO→mono, STEREO→stereo, BUS→stereo). This preserves existing native DRY Show and old client payloads. Typed values on v1 are rejected rather than silently ignored.
- Version 2 requires all 10 rows' **sourceKind** NONE/PHYSICAL/BUS; **sourceFormat** MONO/STEREO only for PHYSICAL, NONE otherwise; **channelFormat** MONO/STEREO independent. Legacy `mode` is preserved solely for backward-compatible codec identity; contradictions fail.
- UI generated `schemaVersion:2` only on the unchanged muted routing action; native epoch and revision checks predate parsing, and `NativeDryRouteCommand` cannot partially publish on failure.
- Typed format is a processing policy inside current stereo scratch plan, **not** a promise of native mono-in mono-out VST3 negotiation. Existing `prepareStereoEffectLayout()` checks still apply; full vendor bus negotiation/PDC is MS2/MS4. Mono physical→stereo processing uses dual-mono scratch before existing stereo VST; no invented spatial width.

## Native DSP correctness fix
`LiveDryRoutingLab`'s realtime strip-control constructor previously set the mono PAN flag strictly from `effectiveFormats==MONO`. A PHYSICAL MONO input→STEREO channel would therefore switch to stereo BALANCE after a live fader/PAN edit and produce a +3dB center discrepancy compared with compiled native graph gain. The prepared control plane now sets mono equal-power PAN when **source=MONO OR effective channel=MONO**; no new callbacks, allocations, heap graph, GUI scheduler or hotplug path.

## Required gates
- Native `tests/native_dry_route_command_test.cpp`: v1 compatibility, v2 type schema, physical mono→stereo, disconnected MONO BUS receiving native PRE, PCM level, missing/contradictory format rejection and unchanged decoded graph; existing `tests/channel_routing_contract_test.cpp` verifies sparse ports, stereo→mono 0.5 fold and negative rollback.
- `tests/mixer_io_operator_contract_test.mjs` cross-checks one controller, genuine typed native decoder, lightweight bank control, schemaVersion 2 and stable layout. `tests/mixer_io_smoke.py` verifies source/format independence across MIX/PATCH, negative ACK and reverting, 11 fader tops unchanged across six mode transitions.
- `scripts/build_ui_css.py --check` must show standalone managed mixer CSS/JS byte parity; standalone preview has disabled native-only format selector when no backend, not fake LIVE.
- Exact PR HEAD Chromium + Windows MSVC/CTest/native package must PASS before merge. For release, operator FAT should verify 2×2 and at least one sparse ASIO map, plugin compatibility/negative ACK and physical audio crackle #15 separately; DO NOT claim hardware green from CI.

## Native DRY Show V4 follow-through (MS1 completeness)

The original saved snapshot schema V3 only recorded physical source mode (MONO/STEREO/BUS), which would silently lose independent channel processing width on recall. MS1 upgrades **native Snapshot codec to V4**: every strip persists `channelFormat` integer **1=MONO / 2=STEREO**; legacy V2/V3 decoders infer the original source-following policy (physical stereo=STEREO, physical mono=MONO, BUS=STEREO, NONE=MONO). A forged V3 with a V4 typed field is rejected, and invalid V4 width rejects transactionally. SHOW metadata `ShowV2` stays unchanged.

`NativeLiveShowCapture::captureDry` reads the actual ACK routing decoder's independently accepted width; `NativeDryShowRecall::prepare` outputs a v2 typed routing command with the same `sourceKind/sourceFormat/channelFormat` and accepted epoch/revision, still requiring MUTED/verified hardware with all existing negative gates. `tests/native_show_schema_test.cpp` validates V4 round trip/legacy V2+V3 migration/corruption, and `tests/native_live_show_capture_test.cpp` tests a true v2 accepted native route across capture/codec/recall, plus rejection if hardware pins disappear. This closes MS1's DRY Show format-loss seam only; full VST vendor-state scene recall and FX-equipped Show are still [MS6 #180](https://github.com/masarray/arsonmix/issues/180).
