# DS-02 — contextual detector foundation, 2026-11-25
Main 2253001926e6ef318eb00c9673417dc1177df4d6; PR #901 baseline 4025ac7cb693903e10dd2c354f1033057bd02211; no review threads; generic fast native success, required gate pending. Baseline local core/test/bench hashes matched remote. DS-01 suite rerun unchanged.
Research: https://pubmed.ncbi.nlm.nih.gov/11008825/ (relative amplitude, spectral shape), https://pmc.ncbi.nlm.nih.gov/articles/PMC7297833/ (context-dependent fricative center of gravity), https://pmc.ncbi.nlm.nih.gov/articles/PMC10540850/ (spectral peak/valley contrast), https://www.fabfilter.com/help/pro-ds/using/advancedcontrols (processing/latency tradeoffs). Research motivates evidence, not tuned constants or calibrated classification. No copied source.
Root cause: HF/body ratio overreacts to isolated HF and sustained bright harmonic proxies. Add per-channel 600Hz Q1.5 narrow witness, 85ms peak-decay body memory, 120ms reference [0.02,1], threshold uplift <=5dB, adapt only ratio<=1.3 and current witness>=.08 HF. Authority ramps between bodyMemory/HF .012 and .04. Control tick round(rate/6000) persists across blocks. Cached linear threshold, skip log/pow at zero excess. Same causal shelf, no new buffering, fixed state.
 
Quadrature tone-amplitude cut dB, rates 44.1/48/88.2/96k:
- Isolated 7.2k steady: 4.180/4.255/4.673/4.717 -> 0/0/0/0.
- Stable 800Hz+.10 HF: 1.064/1.097/1.223/1.240 -> 0/0/0/0 (steady-state only).
- Severe 800Hz+.25 HF: 3.593/3.663/4.016/4.060 -> 3.592/3.663/4.015/4.060; body cut .176/.178/.189/.190.
- Unvoiced HF after body: 4.180/4.255/4.673/4.717 retained.
- Strong .35 HF after bright conditioning: 3.007/3.061/3.308/3.346, near gate at 44.1k.
- -40dB input variant within .0002dB. Amount .25->.5 at fixed Range8 +1.595..1.802dB; .5->1 at fixed Range12 +1.810..1.959dB. At Range8 .5->1 can saturate; attempted >=1dB failed, so test unsaturated region, not reduce original gate.
- Reference learning/freeze/recovery/reset, one-sided stereo parity/no crossfeed, prior unity/partition/antiphase/invalid settings pass.
 
GCC 14.2 Release CTest 2/2 (0.19s); Clang 17 Debug ASan/UBSan 2/2 (1.55s). Local blobs Context 4eacd27d398eb138a0ce19962ac8bd83915c1a56, Core 661bde6ddd201eaf8046927e5bf5267d28c9261a, CMake db78ff7a73f49a872e484ffba257a348022a3bc1, context test 624f46104730607969289a992b27e2424093fbbd. Foundation test/bench unchanged.
 
Same 48k stereo benchmark on shared AMD EPYC 9V74, GCC -O3 -DNDEBUG, 10k blocks per size, single run each. Microseconds:
frames | baseline P50/P95/P99/max | candidate P50/P95/P99/max
32 | .821/.831/.851/90.516 | .982/1.002/1.362/68.464
64 | 1.612/1.623/1.713/126.431 | 1.953/1.993/2.794/133.151
128 | 3.215/3.235/4.717/192.892 | 3.885/3.926/5.369/153.121
256 | 6.409/6.450/19.830/253.523 | 7.741/7.812/20.260/349.107
512 | 12.799/12.889/31.869/1346.250 | 15.444/18.378/65.268/1773.220
Median cost +20-21%: fourth biquad/context adds work (inspection, not profiler attribution); accept measured negative-control benefit for now; stage profiling and cheaper witness investigation DS-04. 64-frame P99 2.794us / 1333us period ~0.21%; shared scheduler outliers; not WCET or ASIO qualification.
 
Limits: initial bright onset, initial/long unvoiced fricatives, high pitched voice and whispers may be underprocessed; moderate sibilance may adapt. No real voice/cymbal/hiss/harmonic qualification; tone proxies only. No classifier, source modes, adaptive frequency, GUI/native/Windows/hardware. DS-02 contextual foundation, not complete musical qualification. Next DS-03: noise-like and held-out licensed vocal/instrument fixtures, WAV renderer/Delta, review authority floor. No merge or expensive dispatch.
