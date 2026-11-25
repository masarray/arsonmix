# Anti Noiz fast native development
Independent standard C++20 DSP, no JUCE, ASKP-VST, VST3 SDK, ASIO, WebView, network or root CMake configuration. The same production-intended headers are compiled directly, not a duplicate DSP implementation. Keep host/UI adapters out of the core.
## Local loop (repo root, GCC/Clang; Ninja optional)
```sh
cmake -S tools/anti-noiz -B /tmp/anti-noiz-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DANTI_NOIZ_BENCHMARK=ON
cmake --build /tmp/anti-noiz-build --parallel
ctest --test-dir /tmp/anti-noiz-build --output-on-failure
/tmp/anti-noiz-build/anti_noiz_bench
```
Repeat only build + CTest after editing. Without Ninja omit -G Ninja. Windows developers can use CMake and Visual Studio developer command prompt with Ninja/MSVC; use a separate build directory, omit sanitizers; MSVC compatibility is not yet tested. Keep build output outside tracked source. A fresh build is required when switching compiler or sanitizer configuration.
## CI
Scoped anti-noiz-fast-dsp.yml, exact PR head, parallel GCC Release and Clang ASan/UBSan, five-minute timeout, cancel superseded same-PR runs. Only Anti Noiz source/test/tool/workflow paths trigger it (GitHub path filtering uses entire PR diff). No Windows dispatch, no root CMake or shared workflow modifications. Existing generic Windows PR workflow may show its quick Linux job, but its Windows build job excludes pull_request; relevant main push qualification remains intact. Do not merge research commits simply to run CI; no /arsonmix-ci native/full/fat/fast during DSP iteration. On real native product integration, assess feature-scope support before dispatch; if unavailable, use one deliberately scheduled qualified full lane only when needed, not per commit. Never waive protected merge/release gates.
## Measurements
Local Linux, GCC 14.2 and Clang 17, CMake 3.31.6, Ninja. Exact core FFT/test/bench blob hashes verified against GitHub head 3288dbd6c4c9124e798655dce6b7421a5e87a3b0. Tested CMake blob be09cd122cabd69dbd1d5a000a70fde74ef5fceb.
- GCC Release cold configure/build (test and benchmark targets) + CTest 2.41 seconds; PASS.
- GCC unchanged incremental build + CTest 0.07 seconds; PASS.
- Clang ASan+UBSan cold configure/build + CTest 2.65 seconds; PASS.
- This measures current small synthetic suite, not future corpus suite, GitHub checkout/queue, Windows or runtime callback deadlines. Workflow execution must be verified separately.
## Next fast-lane additions
Keep deterministic short regression mandatory; add standalone C++ offline WAV renderer and matched-level A/B/delta output, separately invoked corpus runner, ground-truth labels and contamination tests, per-block P50/P99/max + allocation instrumentation, fixed seeded stress test. Real copyrighted/licensed corpus not committed; document licensing and local manifests. No expensive corpus or 24h stress on ordinary PR push. Add tests to this CMake project as modules grow. Prefer separate .cpp compilation units for larger DSP modules, incremental build and stable interfaces; no speculative library framework or unnecessary dependency. No fast-math until finite/denormal semantics and sonic parity proven.
