# ArSonMix native built-in FX (not FactoryFX/VST3)

This folder is owned by `masarray/arsonmix`. **It does not depend on or modify
ArSonEQ**, whose advanced 24-band/dynamic/M-S/FabFilter-class development remains
independent. `FactoryFX/` elsewhere means separately packaged user/factory
**VST3 plugins**, not native mixer effects. Do not put this source into VST3
scan/packaging flows.

## Initial native DSP module

- `BuiltInFxCatalog.h`: compile-time identity `armix.builtin.channel-eq.v1`
  and stable 8-slot IDs: HPF / low shelf / PEQ 1–4 / high shelf / LPF.
- `ChannelEq.h`: minimal portable C++20, eight fixed slots, max ten IIR biquad
  sections, active-slot fast path, 12/24 dB/oct HPF/LPF, +/-18 dB PEQ/shelves,
  frequency/Q validation, per-instance stereo filter memories, 5ms old/new
  bank crossfade. Live `prepareBandEdit()` computes coefficients on CONTROL;
  `applyPreparedBand()` applies them on AUDIO without trigonometry. No analyzer,
  dynamic EQ, M/S, oversampling, WebView, JUCE, VST3 loader, heap allocations,
  mutexes, FFT or additional sample buffering.
- `CMakeLists.txt`: self-contained standalone headless native CTest using the
  same `tests/builtin_channel_eq_test.cpp` as the main project; no JUCE needed.

## Safe integration contract (not implemented by this folder-only candidate)

One `ChannelEq` per **accepted ChannelId** and audio epoch, initialized at the
actual device sample rate. The existing **single** native audio callback owns
`processMono/processStereo` in-place on its existing prepared channel PCM. An
accepted control command is first compiled using
`ChannelEq::prepareBandEdit()` **off callback**. Its small immutable packet is
handed to the existing bounded control queue and then
`applyPreparedBand()` is invoked on the exclusive audio owner **at a block
boundary**. `setBand()` is an offline / unpublished-graph convenience only,
never invoke it in the live callback (it computes trig). The audio owner can
also call the cheap `resetToFlat()` at a safe block boundary. **Never mutate
the live instance from the WebView/UI/control thread while processing**. DSP has no independent callback or
worker and cannot obtain an audio input device.

Typical chain is verified hardware IN -> trim -> channel EQ -> existing vendor
FX rack -> fader -> PRE/POST sends -> MAIN, but binding this point requires
review of the current single-owner native graph. Default disabled EQ must be
bitwise-transparent. The rack-wide FX bypass is separate from the channel EQ
and must preserve individual insert states. UI is added to the **one approved**
`/preview/` component system only when native snapshot/ACK and Show recall
are implemented; a browser illustration must never claim live audio.

Control packet must carry validated decimal-string ChannelId, device epoch and
base graph revision. After native accept, browser still needs a matching
snapshot with all 8 band settings and computed response. Readback and response
must be prepared from the exclusive owner or an immutable copy off callback;
never sample live biquad histories on another thread. Show save/restore owns
logical settings, not a plugin binary, and must preserve applied revision.
Rapid automation during an unfinished 5ms transition needs explicit owner
queue/coalescing validation before host integration; this candidate does not
claim click-free arbitrary multiwriter automation.

## Validation and release status

This is a **real native DSP and tests**, but **not yet wired to ArSonMix audio
PCM or UI**. No new buttons, routes or VST3 scan are installed by this code.
This is intentionally separate from ongoing P0 mixer recovery and native FX
rack/WIRE PRs. Run standalone CTest; Windows final host build, actual CPU
benchmarks, physical ASIO/driver FAT and accepted GUI integration are separate
release gates. `addedLatencySamples()==0` means no DSP buffering/lookahead;
minimum-phase IIR still has frequency-dependent group delay, and hardware
roundtrip latency is not zero.

## Live PCM seam (P1 integration on the PR branch only)

The existing `LiveChannelFxGraph` is the ONLY native audio owner. The graph
prepares a `ChannelEqBank` using the ACTUAL negotiated sample rate on its
control thread, then calls `render(strip,L,R,frames)` immediately before the
existing vendor FX racks in the SAME `PreparedMixerBlock::process` channel
callback. All 14 lanes are flat and bitwise transparent on first launch.
A new SPSC control-to-audio queue accepts precomputed immutable edits and
an independent bounded ACK ring communicates actual applied settings back
to the message thread, with request ID + original device epoch + graph
revision.  When a previous 5ms fade is running, the next edit waits for the
next block; queue saturation rejects without mutating DSP. Never treat a
QUEUED command as applied, or ACK from an old epoch as a new accepted state.

The isolated graph seam is **not an operator feature** until a host command
adapter, stable ChannelId lookup, state preservation across topology changes,
Show recall and the sole approved `/preview/` control with matching accepted
ACK/snapshot have been proven. This PR must not be merged as a finished EQ
plugin or released as an audible operator feature before those gates pass.
See `tests/builtin_channel_eq_bank_test.cpp` for bounded queue / ACK fixtures.

Reference mathematics: W3C Audio EQ Cookbook (RBJ), 2021,
https://www.w3.org/TR/audio-eq-cookbook/ . Interaction inspiration: FL Studio
Fruity Parametric EQ, **not a copy of its source code, icons, or artwork**.
