# Fast isolated development
From repo root:
    cmake -S tools/de-ezzer -B /tmp/de-ezzer-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDE_EZZER_BENCHMARK=ON
    cmake --build /tmp/de-ezzer-build --parallel
    ctest --test-dir /tmp/de-ezzer-build --output-on-failure
    /tmp/de-ezzer-build/de_ezzer_bench
Separate Clang Debug directory with -DCMAKE_CXX_COMPILER=clang++ -DDE_EZZER_SANITIZERS=ON. No JUCE, root build, WebView or ASIO required. Use focused standalone tests per DSP edit; no full Windows dispatch. Existing protected native/release lanes unchanged. Browser isolated harness and WAV renderer next; native milestone exact-SHA MSVC, FAT instead of redundant native when package wanted. 
