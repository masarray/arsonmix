# Fast isolated development
From repo root:
    cmake -S tools/de-ezzer -B /tmp/de-ezzer-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDE_EZZER_BENCHMARK=ON
    cmake --build /tmp/de-ezzer-build --parallel
    ctest --test-dir /tmp/de-ezzer-build --output-on-failure
    /tmp/de-ezzer-build/de_ezzer_bench
Separate Clang Debug directory with -DCMAKE_CXX_COMPILER=clang++ -DDE_EZZER_SANITIZERS=ON. No JUCE, root build, WebView or ASIO required. Use focused standalone tests per DSP edit; no full Windows dispatch. Existing protected native/release lanes unchanged. Browser isolated harness and WAV renderer next; native milestone exact-SHA MSVC, FAT instead of redundant native when package wanted. 

DS-02: scoped de-ezzer-fast-dsp.yml runs GCC Release and Clang ASan/UBSan on exact PR head because generic fast native does not compile independent CMake project. No existing protected/release workflow changed. No full Windows dispatch.

## DS-03A offline A/B and Delta
After standalone build:
    /tmp/de-ezzer-build/de_ezzer_render vocal.wav wet.wav delta.wav voice 0.5 0.5 8 128
Use auto instead of voice for default DS-02 policy. Keep output filenames distinct and new. Input PCM16/24/32 or float32 RIFF, mono/stereo 44.1–96kHz, no resampling, wet and delta float32, no normalization. Compare dry/wet at matched listening level and listen to Delta at safe level. Delta=samplewise dry-wet, not isolated sibilance. Only authorized/licensed source files; record source, license, checksum and annotations outside repository for private recordings. No real corpus claimed tested. CLI may leave partial output on errors; delete incomplete outputs before retry. No file I/O inside DSP.

DS-03B: offline renderer accepts auto-focused or voice-focused as optional mode strings, with same positional parameters. Old auto/voice unchanged (Shelf). Compare at matched attenuation within annotated events, not identical Amount: Focused typically requires less Amount for a selected band. Listen to untouched dry, wet, Delta at safe levels; no loudness normalization in renderer. Use recordings with verified rights and original native sample rates; 16kHz speech datasets cannot qualify 8–14kHz air preservation, and upsampling cannot restore it. Record source/license/checksum and held-out label intervals; no external recording added.

DS-03C: optional de_ezzer_rt_profile target under DE_EZZER_BENCHMARK=ON; run it after Release build to exercise 12 Auto/Voice and Shelf/Focused stereo 48k configurations with 64/128/256 frames, 10000 timed blocks per configuration, fixed seed. Only Core::process is timed; generation/checks outside measured interval. Report P50/P95/P99/max, period exceedances and finite peak. For same-host baseline comparison compile the same profile against immutable baseline headers. Not a physical ASIO deadline, allocation count, or end-to-end mixer benchmark.
