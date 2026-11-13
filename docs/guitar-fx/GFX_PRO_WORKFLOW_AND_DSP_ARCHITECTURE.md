# Guitar FX Rig — Professional workflow / DSP growth architecture
**Research-anchored decision · 2026-11-13 · PR #607 · Issue #608**

This is an implementation roadmap for an **in-channel native insert in ArSonMix**, not a standalone program, second audio graph, browser-generated sound, or fictional guitar plugin. Harmonice is a separate vocal-harmony plugin.

## Grounded competitive reference (read before adding modules)

| Reference | Verified professional behavior | Product decision |
|---|---|---|
| [NI Guitar Rig signal path](https://docs.native-instruments.com/online-guides/guitar-rig-manual/en/signal-path) and [official product](https://www.native-instruments.com/products/guitar-rig-pro) | Ordered rack/components, readable signal flow; linked browser/presets, cabinet IR loading and global post-rack tools | Keep a compact always-readable chain + collapsible preset library + one focused large hardware editor; do not make a dashboard that always shows every pedal and cab simultaneously |
| [Line 6 Helix Native guide](https://line6.com/data/6/0a020a41143d65fb6bcb2ed8ca/application/pdf/Helix%20Native%20PilotGuide%20v3.00%20-%20Rev%20N%20-%20English%20.pdf) | Snapshots remember changes of enabled blocks and assigned parameters without rebuilding the preset's complete models; preset vs snapshot workflow is distinct | A/B is a first-class durable native tone comparison, not an HTML-only toggle; moving between two bounded programs uses prepared DSP and 128-frame crossfade, with actual callback ACK |
| [NI Cabinet component documentation](https://docs.native-instruments.com/ni-tech-manuals/guitar-rig-manual/en/components-reference) | Real captured cabinet IRs, mic/phase options, explicit cabinet mix | Future licensed measured IRs are a *separate resource path* from amplifier processing; current 65-tap original FIR is explicitly synthetic and is never presented as a measured cab/microphone |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) and [MIT LICENSE](https://github.com/sdatkinson/NeuralAmpModelerCore/blob/main/LICENSE) | Portable native DSP, separate plugin and training repository, model-benchmark tooling; library code under MIT | Evaluate model backend as optional, reproducibly pinned DSP dependency after measured CPU/latency tests; do not imply all downloadable model files have MIT redistribution rights |

## Implemented M1 shape

`Hardware Hi-Z → existing ChannelId → Guitar FX Rig → channel FX/fader → WIRE → BUS/MAIN → physical output`.

- One existing JUCE audio callback and existing FxInstance; `NativeBuiltInFx` supplies catalog identity. Native editor is opened by the existing editor lease.
- Original PNG art is compiled into the same installed executable. PNG is *decorative only*; controls, labels, values, availability, and meter are native and honest.
- First DSP graph: `GATE → OVERDRIVE → AMP → CAB`. Exactly these four modules are prepared and rendered. No fake compressor/reverb/mic-position/tuner audio control.
- One bounded `GuitarFactoryTones.h` catalog is authoritative for both DSP values and factory preset names. Custom is detected by **complete** program equality, including bypass, all parameters, block IDs and order.
- A/B is stored in native `GRX1` state **version 2**: 12-byte magic/version/active-bank header; then two bounded program records (count + 44 bytes per block). Version 1 (single program) migrates by copying its accepted tone into both banks. Invalid bank, truncated bytes, unsupported program, wrong amp/cab order or bad normalized values reject without replacing the accepted processor.
- Program changes are prepared and validated on CONTROL; an SPSC hands them to the existing audio callback, which crossfades for 128 frames and sends an ACK. Pending edits block stale `.smix` snapshot capture. The editor closes without losing the final queued knob or deferred A/B selection while an active processor exists. Stopped audio never fabricates ACK.

## M2 engineering expansion — no naive independent processors per widget

1. **DSP backend contract:** separate immutable prepared program from per-lane audio state. Per-module typed parameters `prepare → process → latency → reset` and bounded state/memory. Keep ChannelId, stable block ID, packet sequence, and ACK exact. A program is accepted atomically, never built in PCM.
2. **Real audio quality:** test aliasing from nonlinear amp/drive with controlled multitone and spectrogram fixtures; benchmark 44.1/48/96 kHz under 64/128/256 samples and 1/4/8 simultaneously armed FX. Decide on 2x/4x anti-alias/oversampling only after measured quality benefit, exact PDC and CPU budget. No unmeasured global 4x default.
3. **NAM backend:** evaluate pinned upstream C++ API and supported `.nam` versions. Load/validate model JSON, weight count, architecture, stated sample rate and runtime buffers strictly **off audio thread**. Keep algorithmic amp as reliable fallback. Audit *individual model* redistribution licenses/provenance separately from MIT core code. Reject unsupported or excessively large models without breaking the last accepted tone.
4. **Cabinet IR backend:** accept only validated PCM WAV and explicit IR licensing metadata, cap file size/length/channels, reject NaN/non-finite samples, resample/precompute on CONTROL, prepare a bounded convolution plan and dual-state handoff with known latency. A decorative mic icon must not move unless the actual accepted DSP IR changes. Preserve MAIN PDC across bypass and FX rack transitions.
5. **Additional stages:** compressor → chorus → delay → reverb once verified native processing and state are implemented. Show only real modules; no "enabled" green indicators for placeholder components. Do not couple internal Guitar FX modules to external VST3 leases or a second device.
6. **Stage controls:** lightweight collapsible preset library, focused amp/cab/pedal, truthful A/B and clip meters, keyboard and future MIDI footswitch mapping; globally stable output trim to avoid loudness jumps **after calibrated tests**. Avoid bulky cards, broad tabs, duplicate routing diagrams and invented live CPU or metering displays.

## Hard acceptance contracts

- Windows MSVC build + actual native registry PCM/restore tests at exact SHA; test 4 factory tones, non-factory Custom, A/B persistence across .smix, v1 migration, wrong bank, corrupt second-bank payload, device restart and bypass edit.
- Fast Linux core tests: no JUCE/no network, deterministic PNG generator, factory preset identity, bounded graph, FIR fixed latency, stereo PCM, refusal of invalid programs.
- Realtime callback: zero allocations, blocking, mutex, disk or module construction; one callback authority, no stale snapshots or premature accepted visual indication.
- Physical audio engineer tests: real guitar/Hi-Z → ASIO processed output, output level safety, SOURCE ONLY/no MAIN, WIRE reconnect, low buffer stress and measured round-trip latency. A Windows CI result cannot prove this; see [GFX field acceptance](GFX_FIELD_ACCEPTANCE.md).

**Out of scope for the current PR:** shipping unlicensed commercial amp/IR images or captures, claiming NAM fidelity, MIDI control before actual tested mapping, a separate standalone mixer, and sound quality approvals based only on CI.
