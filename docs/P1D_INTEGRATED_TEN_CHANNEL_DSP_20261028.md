# P1D — INTEGRATED TEN-CHANNEL DSP MILESTONE (2026-10-28)

**Source-of-truth:** this integration branch originated at `c0d7c794a27fcdc7adbe77c6166a2e97b848b82c` and was subsequently reconciled by a guarded merge with `main@66989fa46f004aa852207f1159bf3e1b8e9506c1` (#101 lean CI). The narrow CMake and Windows workflow edits preserve P0 sample-accurate FX bypass and all device/Orbit protections on main. It consolidates P1A #108, P1B #110, P1C #111 and adds a genuine **single-pass 10-strip audio block executor**. It does **not** silently merge old UI branches.

## Shipped in this integration milestone (native C++, device independent)
- `MixerRoutingPlan.h`: 10-channel gain/pan/mute/solo/master DSP primitives.
- `ChannelRoutingContract.h`: independent MONO/STEREO channel format and explicitly enabled (potentially sparse/noncontiguous) physical input port-to-callback buffer translation. Precompiled mono-to-stereo dual-mono and stereo-to-mono fold policy.
- `ChannelSendPlan.h`: validated max 32 pre/post-fader routes, deterministic DAG order, no feedback/self sends, no accidental duplicate sends, control-thread compile/rollback.
- `PreparedMixerBlock.h`: allocates a bounded planar buffer arena once before realtime. One `process()` actually mixes physical inputs and internal receives, processes channel FX hook, dispatches pre-fader taps, applies independent gains/pan/solo/mute, dispatches post-fader taps, supports returns with NO physical input, sums to stereo master, runs master FX hook and applies master controls. RT path is bounded, no heap/mutex/UI/driver calls; FIXED order and buffer. No one-thread-per-strip.
- Four executable C++ acceptance tests, with asserts enabled in `-DNDEBUG` Windows Release; new `TenChannelEndToEndPreparedAudioRender` test exercises actual audio sample vectors through the full graph, master peaks, post-fader and pre-fader bypass behavior, fanout, returns, SOLO graph closure, invalid routing rollback, no-RT-allocation tracking and 1000 repeated blocks. Older P1A/B/C tests remain.
- Registered in existing `p1-windows.yml` CMake native build + CTest, no duplicate workflow. P0, GUI and plugin files untouched.

## Correctly limited scope
- This graph is an **executable DSP integration**, not merely a planning document, and not an actual live Windows-device implementation. It does NOT replace `src/LiveAudioEngine.h` callback, change physical ASIO capture masks, instantiate VST3, wire new per-strip UI or create an artifact that proves CH2-10 on hardware.
- `Fx` hook is a `noexcept` caller-supplied native function object; **mono-only / mono-in-stereo-out VST3 bus negotiation and plugin instance lifetime are not yet implemented**. The hook is a testable seam, not a plugin adapter.
- The default exact 10-strip / max32 sends / max8192-frame limits are current bounded implementation sizes, NOT a whitelist on the user's VST library or an audio-interface input limit.
- Live production graph changes require atomic block-boundary plan publication, device/graph generation fences, safe de-click ramp/crossfade, non-RT retirement and UI ACK. This milestone intentionally does not bypass those gates.
- Physical device `portId` needs stable identity over session/workspace, driver channel-mask actual mapping, and explicit user action if an inactive pin requires a device restart. Do not silently bind hardware to another port.
- At a live use case with physical upstream AND internal input on same channel, samples sum deliberately. Meter provenance, overload and phase warning need qualification.
- Solo semantics: SOLO strip's upstream ancestors and downstream descendants stay audible; unrelated sends into a downstream return do not leak. MUTE kills all sends in this initial policy; pre-fader sends ignore fader but obey MUTE. FX tails when strips become unassigned need explicit future lifecycle/hold policy.
- Audio stability issue #15 (0-FX crackle) is not solved by this graph; **Windows live hardware FAT is still blocking production activation**.

## Follow-on hardware integration: not marked completed
1. Finish #15 0-FX connected-audio proof for single-clock ASIO, virtual input, WASAPI/BT and real render underrun provenance.
2. Implement device-port registry/control thread; negotiate active channel masks and map exact JUCE callback pointer order for enabled ports.
3. Create channel-specific VST instance/bus layouts and a graph publisher with revision+epoch+ACK and RT-safe retirement. Reuse validated plugin instances without concurrent process in old/new graphs.
4. Enable CH1–10 native routing in production only after hardware/device identity guards and telemetry; then render truthful native meters and compact SOURCE + MONO/ST inspector with no heavy blur / animation.
5. Complete AUX outputs/sends/multi-output patchbay, PDC and portable performance restore, with measured hardware validation.

**No fabricated claim:** CI success only confirms software contracts and Windows compilation, never sound quality, hardware routing, or low-latency audio.
