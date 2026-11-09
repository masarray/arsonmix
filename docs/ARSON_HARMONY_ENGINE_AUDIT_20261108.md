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

## 2026-11-09 follow-up — pitch CPU and measurable callback budget (post-merge)

- Roll the shifted window's energy from one candidate lag to the next. Keep the existing dot-product, confidence threshold, quantizer and FX/PDC/GUI contracts. Each adjacent lag changes by one outgoing + one incoming squared sample instead of recomputing 160 squares. Only floating-point rounding may differ.
- Extend deterministic C++ PCM regressions to a multi-harmonic A2 sample with 64/128/257-frame chunks. Existing E2/C6, dry-latency alignment, automation and music-guide tests remain in force.
- Add `NativeHarmonySyntheticCallbackCpuProfile` to CTest. Synthetic engine-only p50/p95/p99/max data covers 48 kHz (64/128/256 samples), 96 kHz (128 samples), with 2/5 voices. Timing is recorded only outside engine->process(), but Windows runner contention makes absolute timing unsuitable for a CI hard gate.
- No quantitative speedup claim without controlled before/after on one machine. No ASIO driver, JUCE graph, real-world multichannel CPU, formant quality or ear monitoring is measured by this synthetic profile. Physical singer FAT remains release blocking.

## 2026-11-09 P0 bounded realtime engine milestone

**Release scope**: Stable 48-kHz 1536-sample (32ms) DSP lookback and exact native PDC remain unchanged until recorded singing/listening establishes a safe lower-latency algorithm. No experimental formant shifter or hidden direct monitor path is enabled merely to advertise latency gains.

- Move pitch autocorrelation off one large sample spike: snapshot a fixed decimated window every ~6.7ms, process at most 32 candidate lags per sample-scheduled slice, finish before the next analysis window. All snapshots and per-instance state are fixed-size, and graph/audio ownership is unchanged. This reduces the worst-case single-sample analysis work even at ASIO 64.
- Keep the sliding lagged energy improvement from previous #485, preserving confidence thresholds, song key readback, five-voice controls and existing tests.
- Recompute voiced gate and ratio smoothing coefficients on `prepare(rate)` only so physical time constants remain the same across 32–192kHz device reopen; 48kHz response stays calibrated to the earlier engine.
- Enforce deterministic output for identical stereo singing PCM across 64/127/256 sample callback partitioning and instrument the maximum completed pitch-lag work per sample. Retain all prior E2–C6, cross-channel Music Key and dry 192k latency tests.
- The synthetic engine-only CPU profile is diagnostic, not a hardware underrun oracle. Next **non-automatable** P0 gate: independent actual Windows ASIO interface, human singing + recorded dry/wet FFT/formant comparison, 48/96k with 64/128/256 periods and 2–5 voices for 30 minutes; callback p95/p99/max, driver XRUNs and measured loopback round trip must be documented.
- Formant-preserving pitch synchronous synthesis (TD-PSOLA / equivalent) remains a separate replaceable shifter milestone: it must be verified against consonants, onset/off pitch, male/female vowels and CPU/latency tradeoffs before becoming audible default. Never claim today's granular shifter preserves formants.

Project status: P0 **realtime boundedness implementation** awaiting exact-HEAD Windows CTest and singer FAT; not a final professional-quality or latency qualification.

## 2026-11-09 P1 — stereo phase-resilient Harmony analysis

Both `HarmonyDsp` vocal analysis/grain source and the cross-ChannelId `MusicKeyDetector` used `0.5*(L+R)`. Valid `L=-R` PCM from an extreme stereo widener or polarity-inverting source disappeared completely from both. The read-only music source remained audible elsewhere but Harmony incorrectly fell back to Manual.

`StereoPhaseSafeFold.h` now provides a **per-instance, callback-owned, constant-work** fold with precomputed 20ms polarity evidence and 15ms smoothed recovery, strict -0.78 engage / -0.55 release hysteresis, silence release, and stereo analysis-only polarity repair. Ordinary coherent stereo keeps the existing sum. Neither dry stereo, audio routing, FX rack, SPSC schema, 2–5 voices, output device, declared latency nor GUI changes. No heap, locks, timers or per-sample trigonometry.

PCM regressions: anti-phase singer A3 and nonzero five-voice harmony at 48/96k; anti-phase music C major at 48/96k, silence/Manual fallback; real native other-ChannelId antiphase A-minor guide with read-only source samples, plus preexisting in-phase, rekey, block parity, PDC and state migration. This only prevents destructive analysis cancellation, it is *not* formant preservation or ASIO/ear latency certification. Physical singer FAT remains open.

## 2026-11-09 P0/P1/P2 tone and CPU milestone — exact speaker-independent tests

**P0 boundedness**: Preserve exactly the native two-grain shifter's 1536 samples of 48k lookback and its 32-lag-per-slice pitch work. Do not promise latency reduction, physical ASIO stability or true formant preservation without human singing and measured loopback; this commit instead ensures that deterministic pitch ratios and timbral control stay safe across devices.

**P1 tone parity**: Extract `RateStableColor` (257 precomputed floats/instance). The original 48k one-pole coefficient `0.07 + color*0.77` is retained within float precision; at 32, 44.1, 96, 192k convert that same attenuation to its equivalent physical time. The smoothing envelope remains separate. Lookup/interpolation occurs only once per active processing sample and is shared across all active harmonies plus lead correction. Control-thread `prepare()` alone uses `std::pow`. Tests compare 10ms decay consistency across all five device rates and seven Color settings.

**P2 realtime hot path**: Five extra `pow(2, semitones/12)` calls per pitch detection hop were replaced with a 49-entry immutable, off-callback prepared interval ratio table and one reference ratio exponent. Precomputed semitone intervals are combined by multiplication, with output clamp retained. Exhaustive deterministic test covers 12 keys, both scales, 71 degrees, five detunes and all five voice intervals (approximately 42,600 ratios). Maximum deviation from the former formula is below `1e-6` absolute in tests. No new state packets, allocations, locks, threads, routed audio, FX slot changes or GUI code.

**Release gate**: This is source-level algorithmic hardening; the CI synthetic CPU profile is diagnostic only. Additional real voice/female/male formant comparisons and 64/128/256 ASIO buffer loopback/driver XRUN evidence remain mandatory.

## 2026-11-09 P0 bounded-pitch work and hostile PCM follow-up

**Actual source-level issue:** 32 candidate lags × 160 dot-product taps were still evaluated in a single callback sample, causing an avoidable periodic CPU spike even when the average synthetic CPU looked acceptable. Reducing the per-sample slice width without shortening cadence would cause silent abandoned pitch windows. This checkpoint changes BOTH: `maxPitchLagsPerSlice=8` and `pitchSliceEvery_=detectEvery_/32`. Total candidate-lag dot products and 48k grain/PCM/PDC are unchanged. Every snapshot finishes with headroom before the next analysis hop on all 32–192k supported sample rates, with separate completed/abandoned window counters for test-only diagnostics.

**P0 hostile PCM fail-closed:** Native input guards now clip finite >4.0f amplitude before the internal dry/grain histories (the engine already clamps all output to +/-4.0f); NaN and infinity still become silence. `StereoPhaseSafeFold` returns the bounded fold it actually uses for correlation, instead of computing an unbounded sum after clipping evidence. This prevents a host supplying finite near-FLT_MAX PCM from poisoning Music Guide or vocal analysis with infinity. Ordinary valid PCM [-4,+4], channel polarity, dry PDC, Music Track read-only routing and GUI are untouched.

**Qualification:** 5 sample rates (32/44.1/48/96/192k), 3 callback partitions (64/127/256 frames), deterministic A3 voiced note, complete correlation windows/no abandoned windows and <=8 candidate lags per slice; real 2/5-voice hostile IEEE PCM and direct near-FLT_MAX stereo guide. The existing 48k blockwise PCM parity, low bass/high soprano, 48/192k stereo dry and 1536-sample PDC regression remain active. Hardware ASIO and spectral/formant gate remain unverified; no claim of a completed P0 low-latency/formant engine.
