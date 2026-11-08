# ArSon Vocal Throw — native phrase-aware live delay

**Status: functional prototype on a separate feature branch; not a certified live-performance release.**

This is a first-class native ChannelId insert in ArSonMix's single JUCE PCM render graph, not a second audio callback, browser-generated sound or independent DAW.

## Implemented

- **Manual:** rolling ring buffer holds up to 1.5 seconds of the most recent vocal. THROW captures a word or phrase and schedules up to six decaying repeats.
- **Auto:** energy envelope, minimum voice duration, silence threshold and cooldown detect the end of a sung phrase without letting the wet echo re-trigger itself.
- **Hybrid (default):** phrase-end automation and manual cue together.
- **Tempo:** BPM entry (60-200), TAP BPM, quarter/eighth/dotted-eighth/half note subdivisions. Interval timing is in native samples at the actual device rate.
- **Mix:** direct dry input with zero added samples, stereo echo, decay, short fades, and ducking while the singer is active.
- **Real-time:** all rolling/frozen buffers are allocated off-callback; a fixed 24-stereo-frame-per-sample capture copy budget avoids a single large callback memcpy. A lock-free SPSC transfers host settings and manual cue to the audio callback. Callback never allocates/logs/locks.
- **Identity & persistence:** stable native catalog ID; ChannelId, device epoch, graph revision, and insert instance fencing. DSP completed ACK and later readback precede accepted display. Schema-versioned 48-byte AVTH settings blob validated during Show restore; audio capture and tails are not persisted.
- **UX:** a compact FX editor inside the exact approved /preview/ mixer; presets in header, uniform power/close, BPM/note/repeat, THROW, TAP and collapsible advanced controls. One shared FX stylesheet.

## Architecture

Hardware input → ChannelId mixer strip → upstream EQ/Compressor/VST3 → VocalThrowProcessor → downstream inserts → fader/pan → WIRE/BUS/MAIN → hardware output.

Core files: engine/builtin_fx/VocalThrow.h (DSP), VocalThrowProcessor.h (JUCE), src/LiveAudioEngine.h (graph install), src/Main.cpp (validated command/readback), src/NativeMixerSessionFxRestore.h (versioned state), scripts/compose_native_peq.py (one picker), web/approved-native-bridge.js (real insert), site/preview-v2/assets/mixer-native-vocal-throw.js (controls only).

## Tests / qualification

The ARMixNativeVocalThrowTests CTest covers zero-added-sample dry, tempo divisions, captured stereo echoes after manual cue, auto-end detection and no self-trigger, bad parameter rejection, 8/44.1/48 kHz sample rates and a global allocator guard around realtime processing. Python native picker/compiled-gui tests enforce no extra mixer screen and real-ACK event names.

Before shipping: exact-SHA Windows MSVC + CTest, actual approved /preview/ Chromium screenshots, ASIO 64/128-frame stress at 44.1/48/96 kHz, live singer audition, device restart, bypass/tail behavior, and Show restore.

## Explicit limitations

1. **Automatic BPM analysis from the music/backing bus is not yet implemented.** Auto currently means automatic vocal phrase-end trigger, not auto-estimated music tempo. BPM is entered or TAP-derived. Full beat tracking requires an independent music-bus feed, beat confidence and phase tracking.
2. Voice end detector uses an adjustable energy-envelope heuristic, not linguistic speech recognition or a trained singing model. Breaths, falsetto and microphone bleed can cause false cues.
3. No MIDI footswitch binding, simultaneous phrase layers, stretchable echo phrases, global music BPM transport integration, or direct song-snapshot binding in this milestone.
4. Green quick CI is not equivalent to real Windows native compilation or ASIO device certification.
5. Existing channel routing and independent secondary audio clock remain authoritative; the FX insert makes no new claims about unresolved WIRE/SOURCE ONLY issues.

## Field test

Insert ID armix.builtin.vocal-throw.v1 into real ChannelId vocal strip; confirm graph ACK; sing “atau tidak” then press THROW at 72/95/120/160 BPM; try Auto with varying held notes and short breaths; toggle bypass while dry vocal remains direct; change device/quantum and reject stale instance requests. Compare physical ASIO callback xruns, stereo/feedback, click-free fades and singer subjective quality.
