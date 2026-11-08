# ArSon Vocal Throw — native phrase-aware live delay

**Status: functional prototype on a separate feature branch; not a certified live-performance release.**

This is a first-class native ChannelId insert in ArSonMix's single JUCE PCM render graph, not a second audio callback, browser-generated sound or independent DAW.

## Implemented

- **Manual:** rolling ring buffer holds up to 1.5 seconds of the most recent vocal. THROW captures a word or phrase and schedules up to six decaying repeats. Two independent tail voices allow a subsequent phrase to overlap an existing echo instead of cutting it.
- **Auto:** energy envelope, minimum voice duration, silence threshold and cooldown detect the end of a sung phrase without letting the wet echo re-trigger itself.
- **Hybrid (default):** phrase-end automation and manual cue together, with manual-cue suppression of a subsequent auto event for the same phrase.
- **Tempo:** BPM entry (60-200), TAP BPM, quarter/eighth/dotted-eighth/half note subdivisions. Interval timing is in native samples at the actual device rate.
- **Mix:** direct dry input with zero added samples, stereo echo, decay, short fades, smoothed wet edits, and ducking while the singer is active. A bounded active-tap gain normalizer prevents dense repeats/overlapping phrases from amplifying the wet bus simply because taps stack; dry is never attenuated.
- **Real-time:** all rolling/frozen buffers are allocated off-callback; a fixed maximum 24-stereo-frame-per-sample capture copy budget per active tail avoids a single large callback memcpy. Contiguous copy chunks eliminate per-frame modulo, and the ring write cursor rolls over without division. A lock-free SPSC transfers host settings and manual cue to the audio callback. Callback never allocates/logs/locks.
- **Identity & persistence:** stable native catalog ID; ChannelId, device epoch, graph revision, and insert instance fencing. DSP completed ACK and later readback precede accepted display. Schema-versioned 48-byte AVTH settings blob validated during Show restore; audio capture and tails are not persisted.
- **UX:** a compact FX editor inside the exact approved /preview/ mixer; presets in header, uniform power/close, BPM/note/repeat, THROW, TAP and collapsible advanced controls. One shared FX stylesheet.

## Architecture

Hardware input → ChannelId mixer strip → upstream EQ/Compressor/VST3 → VocalThrowProcessor → downstream inserts → fader/pan → WIRE/BUS/MAIN → hardware output.

Core files: engine/builtin_fx/VocalThrow.h (DSP), VocalThrowProcessor.h (JUCE), src/LiveAudioEngine.h (graph install), src/Main.cpp (validated command/readback), src/NativeMixerSessionFxRestore.h (versioned state), scripts/compose_native_peq.py (one picker), web/approved-native-bridge.js (real insert), site/preview-v2/assets/mixer-native-vocal-throw.js (controls only).

## Tests / qualification

The ARMixNativeVocalThrowTests CTest covers zero-added-sample dry, tempo divisions, captured stereo echoes after manual cue, auto-end detection and no self-trigger, bad parameter rejection, 8/44.1/48 kHz sample rates and a global allocator guard around realtime processing. Additional deterministic tests cover 8/48/96 kHz two-tail preservation during a second cue, a full two-tail pool reporting cue refusal rather than overwriting a running phrase, suppression of manual/auto double-cues, and peak containment of dense six-tap echoes. Python native picker/compiled-gui tests enforce no extra mixer screen and real-ACK event names.

Before shipping: exact-SHA Windows MSVC + CTest, actual approved /preview/ Chromium screenshots, ASIO 64/128-frame stress at 44.1/48/96 kHz, live singer audition, device restart, bypass/tail behavior, and Show restore.

## Explicit limitations

1. **Automatic BPM analysis from the music/backing bus is not yet implemented.** Auto currently means automatic vocal phrase-end trigger, not auto-estimated music tempo. BPM is entered or TAP-derived. Full beat tracking requires an independent music-bus feed, beat confidence and phase tracking.
2. Voice end detector uses an adjustable energy-envelope heuristic, not linguistic speech recognition or a trained singing model. Breaths, falsetto and microphone bleed can cause false cues.
3. No MIDI footswitch binding, more than two simultaneous phrase layers, stretchable echo phrases, global music BPM transport integration, or direct song-snapshot binding in this milestone. When both frozen-tail slots are busy, further THROW requests are rejected (native ACK reports not applied) rather than cutting an existing musical phrase.
4. Green quick CI is not equivalent to real Windows native compilation or ASIO device certification. Zero reported PDC applies only to the FX DSP's dry path; end-to-end hardware/driver latency and callback CPU load still require ASIO measurements.
5. Existing channel routing and independent secondary audio clock remain authoritative; the FX insert makes no new claims about unresolved WIRE/SOURCE ONLY issues.

## Field test

Insert ID armix.builtin.vocal-throw.v1 into real ChannelId vocal strip; confirm graph ACK; sing “atau tidak” then press THROW at 72/95/120/160 BPM; try Auto with varying held notes and short breaths; toggle bypass while dry vocal remains direct; change device/quantum and reject stale instance requests. Compare physical ASIO callback xruns, stereo/feedback, click-free fades and singer subjective quality.

## 2026-11-08 — performance and musicality hardening audit

Audit of the first native revision identified these concrete risks:
- Triggering a second phrase reset `playing_` and overwrote the frozen buffer of the first tail; audible repeat discontinuity was possible.
- The original capture copied 24 frames via two per-frame ring modulo operations; this was an avoidable callback hot-path cost.
- Hybrid manual cues could be followed by auto-trigger of the same sung phrase, producing unintended double repeats.
- Wet level changed immediately on edits; close-spaced six-tap feedback-like echoes could produce unnecessarily high wet peaks.

Hardening: two independent frozen voices, bounded cue admission (explicit rejection if full), bulk contiguous ring capture, write cursor without modulo, continuous smoothing of wet/duck gain, current-phrase manual suppression of auto replay, and peak-aware wet overlap normalization. Silence counters are now saturating for multi-hour sessions.

**Realtime caveats:** No heap, locks, logging or file operations in `processStereo`, `throwNow`, or parameter application. Worst-case processing still includes two simultaneous 24-frame copy budgets, and an up-to-twelve-tap mix; on low-buffer ASIO that requires measured xrun and callback-duration evidence, not just source inspection. Extra frozen buffer memory is allocated only on channels with this effect. A full two-voice pool intentionally rejects a new cue, so polyphony and cue policies should be evaluated with vocalist/operator tests.

**Next release gates:** exact-SHA MSVC CTest and native JUCE compile, 64/128-frame ASIO at 44.1/48/96 kHz, overnight stability and lifecycle/device-rate restart, phrase-ending listening sessions including breath/bleed/falsetto, bypass/re-enable and FX graph routing, and latency/xrun baseline comparisons versus bypass. Beat tracking from an isolated backing/music bus (confidence, phase and half/double-time hysteresis) remains a separate architecture milestone, not an already delivered feature.
