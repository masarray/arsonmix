# AN-02A — offline audio evaluation infrastructure
Base at start: ae5e05bbd29bcaefe14963dcc0f9ccdd2deabab9. Concurrent assembler fix 53600cbf4666ac549747e1334a169d25409d38cd retained without overriding GUI source. Previous focused Anti Noiz CI success; global quick native and UI failed because nested assets were rejected by Pages assembler. Concurrent fix moved the single editor source to tools/anti-noiz/web; future native integration must promote that same source through the approved flat asset contract, no duplicated editor.
## Delivered
Pure C++20 offline renderer uses actual AntiNoizCore, not a DSP simulation. RIFF WAV PCM16/24/32 or IEEE float32, mono/stereo, 44.1–96 kHz, strict RIFF sizes/alignment, file and decoded PCM cap 128 MiB each; WAVE_FORMAT_EXTENSIBLE, RF64, compressed audio not supported. Noise-only reference matching rate/channels, preferably 3 seconds; reject incomplete learning. Keep learned PSD while restarting audio timeline, flush and remove exactly reported latency, retain duration, emit 32-bit float processed and original-minus-processed delta (no auto-normalization, so gain comparisons remain valid). No output overwrite in ordinary single-process use. File I/O and allocation offline only.
From root:
```
cmake -S tools/anti-noiz -B /tmp/anti-noiz -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/anti-noiz --parallel
ctest --test-dir /tmp/anti-noiz --output-on-failure
/tmp/anti-noiz/anti_noiz_render noise.wav input.wav /tmp/voice-test voice
/tmp/anti-noiz/anti_noiz_render - music.wav /tmp/music-test music
```
Listen to input, processed and delta at matched playback volume (delta may contain desired content as well as noise). RMS statistics are not SNR, perceptual quality or proof of classifier accuracy. No user audio uploaded or included.
## Evidence
Hash-verified local source blobs: core 3bfa4d1b7594a734dbcb2bad0af54d6633873452, OfflineAudio cb2ff9ce206209f9ad81772837e1d1a4e71fc7ee, CLI fcc07e09707756379944b38dcaaa75810def5871, standalone CMake d94b61854efbcdc951be482515dfeb40dbfc0207, offline regression 6a984f8d86b8b8c4539c3a643d99d9b99a31eb21. Local GCC Release 3/3 CTest pass (0.14 s), Clang ASan/UBSan 3/3 pass (0.41 s). Synthetic independent stereo noise with two different tone signals and one second pause: 35.6213 dB idle RMS reduction measured within pause, not active speech. Music delta RMS <1e-7, block parity 64 vs 127, delta identity, float WAV roundtrip, incomplete learning, overwrite and truncated WAV rejection pass. Real voice/music corpus, classifier, CPU P99, MSVC, native GUI host and ASIO not qualified. Local Node contract passed on equivalent editor with flat path before concurrent reconciliation; new exact GitHub head CI must be checked separately.
## Next
AN-02B labeled corpus and conservative temporal context detector; no automatic aggressive voice classification until validation. Add clean/noisy reference comparison with transition/onset/tail masks and measured active speech improvement. Keep fast CI; no full Windows dispatch in this milestone.
