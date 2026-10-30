# DYN1B-2A — Legacy 10CH+4 AUX → Dynamic ChannelId PCM migration gate
Date: 2026-10-31 · Product: ArSonMiX · Epic #192

## Goal and exact scope
Build a **control-thread-only**, fail-closed bridge between the validated incumbent native
\`PreparedMixerBlock\` and the new dynamic \`DynamicPreparedMixerBlock\`. No second JUCE
callback or falsely enabled +CHANNEL. No new plugin binary. The existing stable FX
owner, device/epoch/revision MUTE/ACK, factory plugin approval, physical MAIN/MONITOR,
OUTPUT1 secondary clock bridge, and premium mixer GUI remain unchanged.

## One transaction
1. Validate the actual legacy channel/physical-port, BUS, send and AUX layouts using
   \`PreparedRenderGraph::compile\`. The port argument uses the **true callback ordering**
   (e.g., physical labels 3,0,4,7); never infer callback ordinal from GUI label.
2. Create a stable ChannelId map: input[0..9] → channel IDs 1..10; AUX[0..3] →
   IDs 11..14; MAIN → 15; optional MONITOR → 16. This mapping is for one
   immutable legacy session migration, not a product-wide static channel cap.
3. Preserve independent source format, fader/pan, mute/solo, BUS send PRE/POST and
   **send PAN**, AUX return gain/mute/MAIN enable, and MONITOR POST tap for each
   input/AUX. Active AUX is a caller-owned FX slot (metadata-only generator marker);
   **no plugin instance is created or copied** during graph compile.
4. Compile the one ChannelId-based MAIN+MONITOR PCM graph with budgeted scratch.
   True mono MASTER folds only **after** the master FX and master balance/gain,
   consistent with legacy \`finishMaster\`. The output PCM remains separate from
   the monitor mix.
5. Only after both validations succeed is the candidate moved into the caller-owned
   result. Invalid physical ports, non-finite monitor levels, insufficient scratch
   or invalid sends must leave the previous accepted PCM renderer untouched.
   Native live publication still requires MUTE→APPLY→revision/epoch ACK.

## Actual regression evidence
An expanded existing \`ARMixMixerRoutingTests\` C++ target runs the **same 64-frame
real PCM** through the old \`PreparedMixerBlock\` and migrated
\`DynamicPreparedMixerBlock\`: sparse hardware inputs, mono input + stereo input,
internal CH→BUS send PAN, AUX POST send PAN, enabled AUX FX return, independently
fed MONITOR, MASTER VST slot, post-FX master true mono/pan, and a sample-for-sample
comparison with a small floating-point tolerance. Negative cases prove legacy
invalid port, dynamic scratch budget, and NaN monitor rejection/rollback. No
additional CI job or separate DSP callback is introduced.

## Release and architecture boundaries
- **Not yet DYN1B native live activation**. The actual Windows JUCE callback still
  uses fixed 10 input channels + 4 AUX VST3 racks, which MUST NOT be replaced until
  the same graph owns all real plugin leases, per-block fader smoothing, mutes,
  sidechain negotiation, Show recall, CH1 Orbit/editor state, active monitor buses
  and output routing.
- The bridge is control-side parity evidence to reduce risk of one future atomic
  replacement. It does not establish live +CHANNEL, unlimited channels or
  0-latency host/device turnaround.
- Hard gate: exact-head Windows native C++ CTest + ZIP + GUI smoke; after that
  real ASIO/WASAPI hardware FAT (including 0FX crackle #15).
- Avoid future naive UI-only channels, duplicated FX per output, callback allocations,
  per-frame PAN coefficients, implied physical ports or unbounded routing.
