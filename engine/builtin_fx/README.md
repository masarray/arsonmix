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
  bank crossfade. No analyzer, dynamic EQ, M/S, oversampling, WebView, JUCE,
  VST3 loader, heap allocations, mutexes, FFT or additional sample buffering.
- `CMakeLists.txt`: self-contained standalone headless native CTest using the
  same `tests/builtin_channel_eq_test.cpp` as the main project; no JUCE needed.

## Safe integration contract (not implemented by this folder-only candidate)

One `ChannelEq` per **accepted ChannelId** and audio epoch, initialized at the
actual device sample rate. The existing **single** native audio callback owns
`processMono/processStereo` in-place on its existing prepared channel PCM. An
accepted control command is scheduled at an **audio block boundary**; call
`setBand`/`resetToFlat` only on that exclusive owner. **Never call those from a
WebView/UI/control thread while processing**. DSP has no independent callback or
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
