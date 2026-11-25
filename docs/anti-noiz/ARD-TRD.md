# Anti Noiz — Architecture & Technical Requirements v1
## Boundaries
Independent implementation, no private source transfer. One ordinary ChannelId rack insert, existing native PCM render owner, existing FX graph latency and bypass compensation. No new device owner, WebAudio, or fixed global pre-FX bank. Keep public picker unchanged until genuine processor/editor/ACK/state/PCM support.
Stable proposed ID armix.builtin.anti-noiz.v1. Existing extension seam: src/NativeBuiltInFx.h, engine/builtin_fx/FxSpsc.h, RealtimeNativeFxControl, Native preset/Show and approved editor. Use exact FX instance identity, audio-applied ACK and accepted readback. CONTROL prepares/restores/retire; AUDIO owns histories; UI consumes bounded meter snapshots. No concurrent mutation of core from UI. Validate state transactionally; version and rate/device bind learned profile, invalidate on input change. Never serialize raw microphone audio.

## Data flow
Input -> causal context/fast guard + learned PSD -> stereo shared gain policy -> WOLA denoise -> linked residual expander -> aligned output/delta. Separate microphone and music via routing when possible. No exclusive class switching: independent speech, singing, music, useful activity, noise confidence with hysteresis. Fast protection, slower cleaning, stale/unknown context fail open. Model inference is optional control plane only. DSP remains authoritative.

## AN-01 executable baseline
Fixed 256 FFT at <=48 kHz, 512 above 48 kHz; 50% hop, periodic square-root Hann analysis/synthesis, exact N sample delay with zero padding and overlap-add. Full complex FFT uses precomputed twiddles and bit reversal. Explicit noise-only learning, profile retained on rejected learning; no autonomous learning yet. Shared spectral gain from the maximum channel-relative evidence, conservative smoothing and linked frame-level downward expansion; explicit Voice / Singing / Music / Auto policies. Auto protects active/ambiguous content; only noise-like pauses receive cleaning. This is not a validated automatic speech/music classifier. Music policy yields delayed unity. No per-sample allocations, mutex, I/O, worker or dependency. FFT is bounded; processing mode set only by owner. Manual noise learning is not guaranteed to recognize steady musical notes; UI must require a quiet input. No release claims.
N/48k = 5.33 ms at 48 kHz, N/96k = 5.33 ms at 96 kHz; 5.80 ms at 44.1/88.2 kHz. This is NOT the future <=1 ms Live target. Preserve exact PDC; do not silently change latency or advertise zero latency. Separate causal filterbank R&D must be compared against this spectral baseline. No 50/60Hz removal or source separation claim.

## Next implementation sequence
AN-02 corpus-driven classifier: compare Opus-style small GRU vs deterministic multi-feature baseline; independently label mixed content, measure onset recall, false aggressive suppression per second and worst consecutive error. Use past-only features (mel/ERB energy, flux, periodicity, modulation, harmonic continuity); conservative confidence calibration. Use known source metadata; no model artifacts without license audit.
AN-03 selective DSP: decision-directed Wiener vs OM-LSA, audibility/masking and harmonic/transient/tail protection, shadow noise estimate promoted only after verified noise-only frames. Measure clean-signal and active-noise distortion separately; test profile poisoning.
AN-04 native integration: prepared JUCE processor, stereo and mono-layout contract, accepted commands and completions, state version/validation, device restart/bypass/PDC, approved native editor plus picker, actual meters and delta; no second renderer. Keep hidden until supported.
AN-05 performance: benchmark shared FFT, multirate detection, causal filterbank, fixed analysis work and allocation guard; compare 32–1024 block, 44.1–96 kHz, x86 SIMD, measured latency. No unsupported latency claims.
AN-06 exact SHA Windows native/GUI/FAT, corpus and blind listening, cross-thread reconciliation to current main, release only after protected gates. No modification of other FX or routing for AN-01.

## References for public independent implementation
- Ephraim & Malah, IEEE 1984, DOI 10.1109/TASSP.1984.1164453 (decision-directed spectral estimation).
- Cohen, IEEE 2003, DOI 10.1109/TSA.2003.811544 (IMCRA).
- Opus codec source and design: https://opus-codec.org/
- MUSAN: https://openslr.org/17 ; MUSDB18: https://sigsep.github.io/datasets/musdb.html (audit license before training/distribution).
- ITU-R BS.1534 MUSHRA and BS.1116 small impairment tests.
- Klevgrand Brusfri, FabFilter Pro-G, iZotope RX Voice De-noise Music as product benchmarks, not copied implementations.

## Reproduction
From repo root: c++ -std=c++20 -O2 -Wall -Wextra -Werror tests/anti_noiz_core_test.cpp -o /tmp/anti_noiz_test && /tmp/anti_noiz_test
Evidence: see [AN-01 standalone evidence](AN-01-EVIDENCE.md) for hash-verified local C++ tests, measured limits and remaining gates. No installer or product admission.
