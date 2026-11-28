# DS-00/01 local foundation evidence
Audited main 2253001926e6ef318eb00c9673417dc1177df4d6, Anti Noiz PR #900 HEAD 5e70bb38662370e4e6644d075ac61b1bea95289d. Main built-in FX catalog has no de-Ezzer; old demo GUI references DeEsser only. Scoped new paths; no existing file modified.
 
Local GCC 14.2 Release CTest 1/1 PASS (0.04s); Clang 17 Debug ASan/UBSan CTest 1/1 PASS (0.36s, before optional benchmark target addition, test/core unchanged). Exact tested core blob 6be03c9f791b66d8d6cb64bba52eb7a9e202b68f; test 61d4f1687ca39b9d04d82dc9c37ce86244d11528; benchmark ea211b07571b74b5a519102d023a5d9f819ad65f; final CMake 46b5a2b74b2f34490e5ba0d16bcde487b723b68e. Verify remote blobs before associating evidence with PR. Initial 44.1k in-phase HF reduction 2.92743 dB failed >=3 gate. Increase severity mapping 1.35; startup false trigger at 88.2k fixed by 1ms warmup, no gate lowered.
 
Final 800Hz body + 7200Hz burst in-phase projections (not phase-independent amplitude):
rate Hz | HF cut dB | body deviation dB
44100 | 3.79633 | -0.222771
48000 | 3.88630 | -0.226921
88200 | 4.34826 | -0.247694
96000 | 4.40621 | -0.250207
Strong amount AND range increase >1dB; this is not isolated Amount monotonic evidence. Exact zero Amount and clean body unity, arbitrary partitions bit-identical, antiphase stereo, NaN/Inf recovery, invalid settings reject. No labeled vocal or harmonic protection evidence.
 
48k stereo GCC Release Xeon Platinum 8573C, 10000 measured blocks per size, shared Linux:
frames | P50/P95/P99/max microseconds
32 | .952/.974/1.25/184.686
64 | 1.878/1.915/2.687/416.688
128 | 3.736/3.760/5.066/158.923
256 | 7.444/7.490/53.224/298.941
512 | 14.863/15.076/66.255/1850.99
Single timing run only; scheduler outliers, no allocation instrumentation or hardware claim. Added buffering latency 0 by topology; active shelf phase delay is not zero. GitHub CI, native integration, Windows, physical ASIO, real audio quality, GUI and release NOT RUN/NOT IMPLEMENTED at authoring. Next DS-02: true quadrature/energy metrics, adaptive contextual detector, false trigger/weak fricative/bright instrument controls, independent Amount regression and offline renderer. Preserve existing gates.
