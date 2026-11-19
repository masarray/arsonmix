# IO-2 input mapping — native integration handoff (19 November 2026)

**Scope:** isolated IO-2 C++ control-prepared DeviceKey + physical pin -> one native ChannelId graph input map. **Not** yet an installed secondary ASIO source or an audible Windows implementation. Deliberately no existing production code, CMake root, JS or GUI changes.

## Read before consuming this module

- `engine/device/MultiDeviceInputPlan.h` is a deterministic CONTROL compiler (no JUCE) and RT-safe **borrowed** planar-pointer assembler. `tests/multi_device_input_plan_test.cpp` tests duplicate input 1 on devices A/B, sparse pin-to-packed-ordinal mapping, ChannelId mono/stereo, stale device/epoch, actual PCM admission, same-generation device swap and single-device parity.
- First endpoint must be **verified primary**, with generation equal to primary output epoch. Every endpoint also provides a **nonzero native-issued unique deviceLease**; two distinct capture devices can legitimately have the same generation number, so this must not be inferred from the generation. Key MUST come from actual driver/endpoint attestation, not a user-facing friendly name, and does not become permanent physical identity automatically.
- Capture blocks are indexed by CONTROL-prepared device order, and carry matching `generation`, `deviceLease`, `frames`, `channels` and `admitted` readback. A null/mismatched secondary is silent **only for its virtual slots**. The primary epoch mismatch invalidates all slots. The caller must keep borrowed pointers and the immutable plan alive throughout the primary render lease.
- The plan's `source(i)` is a verified ChannelId + virtual `inputL/inputR` pair. Use those as the physical ordinal fields of **the existing** `armix::mixer::DynamicSourcePatch` and set **the existing** `DynamicGraphRequest.physicalInputs` to `plan.virtualInputCount()`; do not create another graph, PCM owner, ASRC, or VST instance. Preserve player-deck source kind and existing accepted WIRE/BUS/MAIN edges. This conversion is a **future CONTROL integration**, not implemented here.
- For each accepted secondary output of `SecondaryCaptureClockStream::pullForPrimary` (PR [#771](https://github.com/masarray/arsonmix/pull/771)), provide its planar scratch as a `CaptureBlock` only after exact capture generation and native lease authorization. **One capture device = one producer/one bridge; the primary callback = one consumer**. Capture adapter and its scratch cannot be freed until both callbacks have quiesced via CONTROL. No callback allocations, locks or driver open/close.
- Native output permission, manual ASIO hardware FAT and independent output BUS sinks are NOT implemented by this module. Missing device and invalid pins reject the CONTROL preparation; the accepted old plan remains unchanged, and `.smix` desired offline binding must be preserved by its existing owner. A future GUI uses ONLY native accepted + installed readback, never a JS-local LIVE label.

## Parallel ownership / merge protocol

At this checkpoint G-MV27 [#811](https://github.com/masarray/arsonmix/pull/811) is merged; N-MOD [#799](https://github.com/masarray/arsonmix/pull/799) remains active and edits `src/Main.cpp` and root `CMakeLists.txt`. **Do not edit these files in this PR.** Keep current GUI bridge, /preview/, VST3 and the existing direct ASIO route intact. Mainline and PR heads may change: refresh them before making any integration edit.

**Standalone regression without modifying root CMake:**

```sh
cmake -S tests/io2 -B /tmp/arsonmix-io2-test
cmake --build /tmp/arsonmix-io2-test --config Release
ctest --test-dir /tmp/arsonmix-io2-test -C Release --output-on-failure
```

This portable CTest is a new independent project, not registered in main native CI by this PR. After N-MOD's root CMake ownership is released, integrate the test into the existing native CTest lane **in one application integration PR** without overwriting Player targets.

**Hard milestone gate after IO-1 reconciliation:** single installed render callback reads B from independently verified IO-1 capture ASRC and admits it into one `DynamicPreparedMixerBlock`; native request/ACK/physical readback and .smix persist separately; tests verify A.Input1 CH01 and B.Input1 CH05, FX->BUS->MAIN once, stale B does not affect A. Only real dual-vendor hardware test can qualify physically simultaneous ASIO.

Related coordination: [#812](https://github.com/masarray/arsonmix/issues/812) · [multi-device ARD v2](../ARD_ARSONMIX_MULTI_DEVICE_IO_V2_20261118.md) · [root AGENTS.md](../../AGENTS.md).
