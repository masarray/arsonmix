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

4. Windows CI first measurement (exact prior PR head bfb815a5, **not** a controlled before/after): 48kHz/256 frames, 1 ImmerSIB, Live Concert ROOM ON, p50 1650.10us, p95 2144.60us, p99 2268.00us = **42.53% of 5333us deadline**, zero DSP-only over-budget blocks in 128 measured blocks. Runner performance is not a low-end PC certification. The expanded five-case quick suite follows to capture 128f, 256f, 2/4 inserts and ROOM OFF. Never report a CPU saving without a matched reference.
5. Windows initial red was at the later **RC->public profile reconfigure**, after JUCE editor/native CTest passed, the P102 quick benchmark compiled/ran and approved 30 PCM source-lock passed. CMake retained cached P102 stage/EQ ON when only parent RC was turned OFF. The focused Windows workflow now explicitly turns both P102 flags OFF **and verifies cache readback**, keeping CMake fail-closed semantics; a Python regression asserts this contract.

### Windows Release usage

Configure the existing Windows candidate with ARMIX_IMMERSIB_RELEASE_CANDIDATE=ON, use Release configuration, then:

    cmake --build build --config Release --target ARMixImmerSIBP102CpuProfile
    build/Release/ARMixImmerSIBP102CpuProfile.exe --quick
    build/Release/ARMixImmerSIBP102CpuProfile.exe --matrix

Executable path varies by CMake generator. Quick: 48k/256/1 Live Concert ROOM ON. Matrix: 44.1/48k x 64/128/256/512 x 1/2/4 x scenes 2/8 x ROOM on/off. Record exact Git SHA, Windows CPU/RAM, release compiler, power plan, temperature and audio devices.

## M2 evidence added: stage attribution and editor-reopen reliability

The P102.3 CPU profiler now offers `--attribution` using the production
`SonKuEarlyReflections` six-surface measured HRIR path and
`SonKuMusicalReverb` eight-line room-aware FDN independently. It records
p50/p95/p99 on the **same invocation**, 44.1/48 kHz, 128/256 frames and
two actual scene configurations, with finite PCM/hash readback. **These are
isolated component costs, not additive percentages of the entire live DSP
graph**: signal feed, cache and warmup differ. They prioritize the next
processor optimization, not claim a measured CPU reduction.

The real JUCE native CTest now runs 100 create/destroy/reopen iterations
against one prepared ImmerSIB processor at **both** supported rates. Every
tenth reopen applies a real scene and verifies post-audio-callback ACK;
every 25th reopen round-trips accepted IMRS state; PCM continues to process
between editors. It does not fake actual HWND or ASIO hardware qualification.

Run additional standalone cost attribution with:

    build/Release/ARMixImmerSIBP102CpuProfile.exe --attribution

### First actual isolated stage measurement (Linux, exact source `ec5bc8f0879c`)

[Linux source CI run 45024178447](https://github.com/masarray/arsonmix/actions/runs/45024178447)
compiled the actual P102.3 EARLY/FDN headers under strict C++20, -O2,
-Wall -Wextra -Werror and ran all 16 attribution cases.
With 48 kHz / 256 frames / one stage, the **isolated** p99 measurements
were: Live Concert 6-wall EARLY 675.89 us versus 8-line FDN 11.94 us;
scene 8 EARLY 627.34 us versus FDN 11.77 us. At 44.1 kHz /
256 frames, Live Concert was 629.59 us EARLY and 16.13 us FDN.
Each stage logs sample finiteness and a PCM fingerprint.

Decision: six-surface FIR is the first stage worth profiling at instruction
level. The FDN is not the first target. **Do not sum these results or
subtract them from full-engine timing**: different signals, caches and
independent state mean they are NOT an additive decomposition. No controlled
before/after optimized candidate exists yet, and a CI runner is not low-end
Windows/ASIO hardware.

The next ACTUAL speed patch must prove unchanged accepted PCM bitwise across
44.1/48 kHz, all 7 scenes, mono/anti-phase/stereo and room ON/OFF,
including restart and live edits. If FIR arithmetic order or voicing changes,
the approved 30-source lock cannot be silently resealed.

No PCM-locked source or user-approved coefficients are changed in M2 evidence.
Full Windows CI, exact-sha checked benchmarks and low-spec physical listening
remain gates for any actual DSP optimization.

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
