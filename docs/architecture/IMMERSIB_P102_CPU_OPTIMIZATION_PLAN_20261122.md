# ImmerSIB P102.3: low-spec performance milestone and evidence gates

Owner field report, 22 Nov 2026: latest alpha.32 runs for hours without crackling on Core i7 / RTX 4050 / 16 GB. Low-end CPU overload has NOT been reproduced. See issue #854. The old no-FX glitch #15 is historical/closed.

## Fixed product constraints

- The approved 7 presets, HRTF/room, bass, EQ and source lock are sacred. This branch does NOT edit any of the 30 PCM source-locked files.
- One native PCM render graph, direct primary ASIO, original IMRS/.smix and editor ACK. No new signal path, latency or GPU requirement.
- Actual per-callback audio budget matters; aggregate Task Manager CPU% is NOT an xrun indicator.

## M1 code in this branch

1. UI: lazy photo acquisition. The approved JUCE ImageCache is retained; only the visible 2200x922 photo is decoded on first paint. Scene switching still uses exactly the existing venue images; native QA ready() deliberately forces validation of all six. No UI or PCM sound redesign.
2. DSP evidence: new CMake target ARMixImmerSIBP102CpuProfile uses the same real P102.3 preprocessor flags as ARMixConsole, and explicitly enables both branded fixed-stage and musical EQ before prepare. Builds outside normal ALL/CTest to avoid unnecessary CI/runtime cost.
3. Output CSV reports DSP-only p50/p95/p99, budget percentage, late-block counts, scene, 44.1/48 kHz, 64/128/256/512-frame and 1/2/4-instance cases. Input synthesis and buffer preparation are outside the timed region. This does NOT measure JUCE host overhead, DPC, driver XRUN or real hardware sound.

### Windows Release usage

Configure the existing Windows candidate with ARMIX_IMMERSIB_RELEASE_CANDIDATE=ON, use Release configuration, then:

    cmake --build build --config Release --target ARMixImmerSIBP102CpuProfile
    build/Release/ARMixImmerSIBP102CpuProfile.exe --quick
    build/Release/ARMixImmerSIBP102CpuProfile.exe --matrix

Executable path varies by CMake generator. Quick: 48k/256/1 Live Concert ROOM ON. Matrix: 44.1/48k x 64/128/256/512 x 1/2/4 x scenes 2/8 x ROOM on/off. Record exact Git SHA, Windows CPU/RAM, release compiler, power plan, temperature and audio devices.

## M2 (not implemented): optimize the actual measured DSP bottleneck

- Record matched unmodified P102.3 baseline versus candidate, on the same computer. Use native callback telemetry and attended 120-second real hardware A/B, NOT one offline benchmark alone.
- Profile 140-tap 2x2 FIR and per-sample coefficient interpolation, ER six-surface, FDN eight-line reverb, P102.3 seven stereo biquads, and host FX insert contention. Choose one hotspot only after a profiler proves it costs meaningful CPU.
- Candidate families: unchanged-target kernel interpolation bypass ONLY if mathematically/PCM identical; more cache-local FIR loop and fixed index updates with bitwise regression; prepare immutable coefficients off RT thread. No speculative FFT, convolution rewrite, GPU, lookahead, or unqualified denorm/fast-math switches.
- The approved PCM source-lock must NOT be silently resealed. First pin exact original PCM/golden source, compare seven presets x two rates x frames x ROOM, impulse/L/R/center/anti-phase, state restore, power/bypass, and audible attended A/B. Changes with non-identical PCM require explicit owner approval and a new versioned profile, not a stealth optimization.

## M3 (not implemented): low-spec Windows qualification and release

- Full mixer + VST3 + ImmerSIB source -> FX -> BUS/MAIN -> ASIO physical hardware, 128/256f, CPU i3/Ryzen 3, 8 GB and mid-range; p99 callback, over-budget, peak RSS, editor cold/reopen, audio continuity over 120 seconds.
- Repeat editor open/close/refocus 100x, native lease/ACK, device restart and .smix restoration. Existing G-MV #836/FUX #853 fixes remain baseline, not a claim of field proof.
- Windows MSVC CTest, approved /preview/ UI and exact-SHA installer gates, protected review, then release. Never claim low-end hardware-certified before evidence.

## Acceptance for M1

- P102.3 source lock unchanged, original audio intact.
- Lazy GUI venue rendering/asset contract and native JUCE editor tests pass.
- CPU profile executable compiles with approved macros, prints valid finite numbers and full matrix without changing audio state.
- Source status is not confused with compiled installer, actual real-time performance improvement or hardware FAT.
