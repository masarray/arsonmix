# DS-04A — real-time dispatch and automation hardening
Main 2253001926e6ef318eb00c9673417dc1177df4d6; previous PR #901 HEAD 5832e723f45dc655f7eb2832ee505a7438c463c5. DS-03C dedicated focused DSP and fast native CI success (45600361282 / 45600361604); required merge gate pending, no review threads. Baseline local Core 7c05b1c6329023e6d61beb587fc7209a8b16db1f and Context 4eacd27d398eb138a0ce19962ac8bd83915c1a56 verified. No changes to shared mixer/GUI/native graph.
 
Optimize without changing DSP policy: one block dispatch to compile-time Voice/Auto and mono/stereo variants of one shared process template. Hoist shape target and Voice threshold. Only convert sensitivity on change; skip Voice authority if HF<=1e-7 (zero authority by existing definition), defer ratio when authority zero. Preserve coefficients, control tick, gain, and stereo weight equations. No fast-math, SIMD, additional latency or callbacks.
 
Local GCC 14.2 -O3 and Clang 17 -O1 ASan/UBSan with warnings as errors: Focus, Dynamic and new Stress tests pass. Stress: 16 primary instances + 16 independent partition replay instances per sample rate (44.1/48/88.2/96k), interleaved on one owner thread, 64,000 primary blocks, 8,276,864 primary frames plus replay. Vary 1..257 frame blocks, mode/shape/Amount/Sensitivity/Range/enabled at block boundaries, NaN/Inf, FLT_MAX, denormal inputs, invalid settings/prepare, 1-second silence then exact settled bypass. Finite PCM and bounded GR/weight; exact partition replay, zero intercepted C++ new/delete calls including aligned forms; positive counter self-check passed. Counts cover audio-owner setSettings, process, telemetry and invalid prepare calls; no claim about arbitrary C malloc, OS allocators or integrated host, no concurrent instances across multiple audio threads. Not a long-duration soak, UI thread safety or physical qualification.
 
Temporary local baseline-vs-candidate differential harness (not committed): exact PCM and GR/weight across 2,066,528 frames, four rates, mono/stereo, 2000 deterministic variable blocks per rate/channel combination, dynamic settings and mode/shape changes; both headers compiled GCC -O3. No tolerance increase. Existing matched-benefit Focus and Dynamic tests pass both compilers. Full seven-test CMake suite not rerun locally; GitHub CI must verify. Exact locally tested Core d0c0a430bbad5ddc8f789c98af1b13e2828cd56e, stress bd572c7ebd898f5dcb527afefce227ccd740dfc8. Context, Focus, Dynamic and profile unchanged.
 
Same-host benchmark (AMD EPYC 9V74 shared Linux, GCC 14.2 -O3 -DNDEBUG, rt_profile.cpp, 48k stereo, 10000 blocks per config after warmup). Paired sequential baseline/candidate run in us, P50/P99/max:
mode/shape frames | baseline | candidate
Auto Shelf 64 | 1.943/3.055/605.805 | 1.703/2.874/5050.77
Auto Shelf 128 | 3.866/23.906/224.73 | 3.385/5.799/188.205
Auto Shelf 256 | 7.712/20.701/691.945 | 6.750/21.141/1136.24
Auto Focused 64 | 2.273/2.343/55.094 | 2.143/2.304/220.433
Auto Focused 128 | 4.527/6.670/269.837 | 4.266/6.430/126.03
Auto Focused 256 | 9.034/31.097/2568.73 | 8.523/23.475/211.199
Voice Shelf 64 | 2.484/3.555/220.493 | 2.334/3.465/144.669
Voice Shelf 128 | 4.947/7.731/154.083 | 4.647/6.930/71.448
Voice Shelf 256 | 9.884/23.946/235.405 | 9.264/24.537/241.835
Voice Focused 64 | 2.734/3.796/225.902 | 2.614/3.866/3153.39
Voice Focused 128 | 5.438/9.194/184.408 | 5.198/8.473/171.429
Voice Focused 256 | 10.846/26.220/354.986 | 10.366/24.687/225.029
Median reductions roughly 4–13% depending on mode. Baseline zero nominal-period exceedances in 120k blocks; candidate 5 (4 Auto Shelf64, 1 Voice Focused64), max 5.05ms. Earlier candidate-only run zero exceedances, but do not discard observed misses or attribute them definitively to scheduling. No WCET or hard real-time pass. Finite peak .370174 for this benchmark. Next qualify multi-instance CPU budget, repeat on controlled host and Windows ASIO; no speculative detector retuning. 
   
Status: standalone targeted code tested; real listening/corpus remains unqualified, no native admission, GUI, Windows installer or hardware FAT. Shelf default retained. Next DS-04B/DS-05: bounded control/ACK/readback/telemetry adapter and isolated UX integration with existing approved preview; close RT qualifications as evidence warrants. No full Windows dispatch or merge.
