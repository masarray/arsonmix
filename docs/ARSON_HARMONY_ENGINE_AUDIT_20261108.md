# ArSon Harmony — realtime-engine audit and qualification plan (2026-11-08)

Scope: [Harmony PR #467](https://github.com/masarray/arsonmix/pull/467), **not** a new PCM renderer, VST3 wrapper, independent mixer GUI or release. Only the approved `/preview/` ChannelId insert owner may render audio. This report distinguishes verified CI from unverified singing quality and physical ASIO use.

## Evidence and P0

- Earlier exact-SHA Windows milestone `42489494648`: 64/64 CTest passed at `4b4a677`.
- Later milestone `42490490079`: 63/64 CTest; `NativeHarmonyMultivoicePitchAndRealtimeFinitePCM` crashed with Windows SegFault. All other 63 tests passed. Root-cause hypothesis from source: nine large stack-local `Engine` fixtures; the last singer-register loop added another ~400 KiB Engine object. The tests now explicitly heap-own each fixture. **This is a source-based diagnosis, not a postmortem stack trace**; exact-HEAD Windows requalification is required.
- No independently observed singer, device, driver, measured callback utilization, or ear-monitor latency in either workflow. Do not interpret MSVC/CTest PASS as aural or ASIO qualification.

## Architecture constraints

`engine/builtin_fx/HarmonyDsp.h` is pure C++ DSP with bounded, fixed storage. `ChannelHarmonyProcessor.h` participates in the existing channel FX rack, receives precomputed control packets through its existing SPSC and reports DSP-applied ACK. No DSP work in JS. Keep UI accepted/revision/instance-gated and maintain VST3, routing and Show save/restore behavior. Real-time callback must not allocate, lock, log, destroy processors, scan plugins or perform control-thread trigonometry.

## DSP findings and work

| Topic | Audited behavior | Improvement in this milestone | Remaining qualification |
| --- | --- | --- | --- |
| Ring-memory footprint | Three 32768-float history rings (~384 KiB, excluding detector/control state) per instance | Three 16384-float rings (~192 KiB), rate/history budget guard intact through 192 kHz; 192 kHz wrapped-impulse regression | Hardware soak and memory budget per many inserted instances |
| Pitch detector CPU | ~175 lag candidates at 48 kHz x 160 taps x ~150 analysis updates/sec; circular indexing repeated for every lag | Gather one contiguous fixed-size analysis window, reuse base energy and process lags without repeated ring addressing; no callback heap | Measure p50/p95/max callback time at 64/128/256 samples and 2/4 voices |
| Pitch musical stability | Nearest diatonic degree with 2-window candidate hold; raw vibrato may cross midpoint repeatedly | Add 0.20-semitone switch margin around incumbent note, require two consecutive analysis windows also on onset; discard stale quantizer after silence/key change | Singing with wide vibrato, glissando, octave jumps and noise |
| Pitch-range support | E2 through C6 test inputs added in prior milestone | Preserve 65–1200 Hz analysis limits | Real female/male phonation, especially breathy and rasp voices |
| Grain pitch shifting | Two-grain smoothstep shifter; `Color` is a simple low-pass, **not formant preservation** | No fictional formant claims; retain deterministic smooth overlap | True voiced PSOLA/LPC or equivalent; ABX on real singer, spectral formant drift |
| Harmony note choices | Fixed diatonic offsets {+2,+4,-2,+6} scale degrees | Hysteresis prevents boundary switching but does **not** infer live chord | Add optional explicit chord/MIDI input and bounded voice-leading, not guesses from an isolated vocal |
| Latency | `grain=16 ms`, reported 2-grain delay ~32 ms at 48 kHz; ASIO, buffer, routing and other FX add more | **Unchanged deliberately**: shortening the grain without quality evidence may worsen artifacts | Quality/Live modes with verified ear-monitor timing and declared exact-sample PDC |
| Control automation | Precomputed gain/pan; ~5ms per-sample ramps | Preserved | Rapid preset/key/bypass and state recall soak |

### Why a "musical" upgrade needs chord context

Selecting thirds/fifths from a *key* is not the same as following the current chord. A single monophonic singer's note does not uniquely identify the chord. A professional-grade voice-leader should take a bounded time-stamped chord context from explicit operator/MIDI/song-section inputs, then minimize voice movement, crossings and dissonance subject to requested ranges. Until such context exists, label output honestly as diatonic harmony, not automatic chord-aware harmony.

### Why the latency number is not a pass/fail metric by itself

A 1536-sample lookback at 48 kHz is **32 ms DSP delay**, before device input + output buffering, additional effects, and ASIO/WASAPI synchronization. Design separate modes only after verifying minimum viable grain/PSOLA quality and aligned dry/wet latency. Avoid dynamically changing plugin latency from the audio callback or rerouting live singer monitoring to a separate device clock.

## Next gates (in order)

1. **P0 fix and regression:** exact-HEAD Windows MSVC + all CTest, no SegFault; 192 kHz circular-wrap and re-key tests; check no regression in all 64+ native tests. Correct source rather than suppress test.
2. **CPU evidence:** realistic monophonic recorded singing, 48/96 kHz, 64/128/256 sample ASIO periods, simultaneous 1/2/4 voices, serial FX. Capture p50/p95/max process time and callbacks exceeding buffer deadline. Release-blocking target: zero callback deadline misses in 30-minute target-machine soak, while documenting observed CPU margins, not inventing percentage gains from source.
3. **Voice quality:** compare dry vocal vs harmonized voice in several registers/vowel/formant conditions; log pitch class/octave error, note onset/release, voiced false-positive rate, intervoice beating, zipper clicks, spectral-envelope deviation and aural ABX with headphones. Use recorded clean and noisy vocal fixtures whose usage rights permit test distribution.
4. **Formant-aware synthesis:** isolate a replaceable pitch shifter interface (voiced pitch-synchronous / unvoiced transient-safe path) with explicit latency and deterministic scratch. Evaluate validated PSOLA and spectral-envelope correction without adding a second audio callback. Third-party library licensing and performance must be checked before adoption.
5. **Context-aware harmony:** optional MIDI/chord/section intent + hysteretic voice-leading with safe fallback to basic diatonic intervals. Keep deterministic tests for major/minor, held notes, modulation, chord changes, no-candidate silence, and stable per-voice identity.
6. **Independent field FAT:** verified Windows artifact, actual audio interface + ASIO driver, live singer monitoring, physical VST3/routing/power and device reopen. This cannot be certified by CI.

## Reference research

- de Cheveigné & Kawahara (2002), YIN, for confident monophonic fundamental tracking and short-window tradeoffs.
- Faghih & Timoney (2022), *Smart-Median: A New Real-Time Algorithm for Smoothing Singing Pitch Contours*; outlier rejection and vibrato stability require evaluation beyond nearest MIDI note.
- Rubber Band v4 technical docs expose formant options and exact latency API, but licensing, ASIO callback suitability and measured overhead must be independently assessed; do not add by assumption.

**Status:** engineering candidate only; PR stays Draft pending exact-HEAD qualification and physical vocal FAT.
