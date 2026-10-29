# R0B B1 — CH1 FX MOVE while 10CH/4AUX native routing is active

## Scope
One small native vertical slice toward issue #149. Builds on merged R0A CH1 external rack lease and P1K dedicated AUX. Not a new session owner or second mixer.

- Previously `moveSlot()` rejected `dryRoutingLab_.active()`, despite the same legacy CH1 physical FX processors already being borrowed into one routed pass.
- It now publishes the requested logical order via existing lock-free atomic `FxOrderPlan` and uses existing `FxMoveRamp` inside the routed callback: fade OUT in one buffer; publish order at zero; fade IN next buffer. CH1 vendor identity, editor, POLAR Orbit, bypass envelopes, and tails retain their existing instances.
- Native MASTER PCM is measured after routed MOVE de-click, with output peak using the actual ramped samples. No new timer, callback, allocations, locks, VST scanning, state read/write or callback detach on the routed RT path.
- Effects on AUX/other lanes are unchanged. ADD/REMOVE/PRESET still carry the old offline restrictions; this is **not** yet unified native startup/full FX lifecycle. The physical #15 crackle FAT remains open.

## Acceptance
- `tests/live_graph_contract_test.mjs` enforces no detach, phase publication, ramp-before-meter, stable CH1 instances.
- Existing `fx_order_atomic_test.cpp` provides deterministic 2-buffer de-click; existing `live_channel_fx_graph_test.cpp` validates CH1 external insert processing.
- Require exact-head Windows MSVC+CTest and Chromium as applicable, and separate ASIO/WASAPI live MOVE/Orbit FAT before any clean-audio claim.

## Follow-on
R0B B2: shared prepared instance/graph ownership and add/remove lifecycle without detaching callback. R0B B3: always-on verified/unpatched silent session startup, device-loss rollback and retirement of legacy/LAB split. Do not claim solved from this slice.
