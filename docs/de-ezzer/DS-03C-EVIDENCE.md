# DS-03C — stereo evidence fusion and dynamic target response
Audited main 2253001926e6ef318eb00c9673417dc1177df4d6 and PR #901 head 9e2e725a9f1c93371fe91a726de4b12dccff8af0, DS-03B focused DSP + fast native CI PASS (45596916524 / 45596916384), required gate pending; no review threads. Exact local baseline Core 69290b81eaaf5f7bae79d8bf3aef4f87dc167b27, Context 4eacd27d398eb138a0ce19962ac8bd83915c1a56, Focus test 174d5c937ba42a807c33ba30a25224602aaef696. No shared file changes.
 
Root cause: Focused selected only strongest channel and shared 8ms smoothing for target and topology. Replace with positive-excess-weighted mean of existing per-channel spectral weights, retaining max excess for shared scalar gain. Separate 2ms frequency-weight smoothing from unchanged 8ms shape smoothing. Same filters and no PCM crossfeed. No delay added; default Shelf retained. No claim of source classifier.
 
Synthetic simultaneous targets (800Hz body plus .25 amplitude 4.6k L/8k R, Auto Focused, Amount1 Range12, quadrature steady cut dB):
rate | baseline L/R | candidate L/R | new lower weight
44100 | 2.08532/10.4688 | 5.13039/5.64584 | .473584
48000 | 2.18426/10.4002 | 5.18895/5.69271 | .47413
88200 | 2.57604/10.1483 | 5.41864/5.87654 | .476131
96000 | 2.60248/10.1325 | 5.43403/5.8889 | .47624
Gate both target cuts >=3dB and abs body cut <.5dB; exact PCM channel-swap and random partition parity. Baseline fails both-target gate. New passes. This is a deliberate balance tradeoff, not matched per-channel benefit.
 
Alternating 20ms 8k/4.6k segments after 300ms low-band warmup; lower weight at 10ms into each segment:
44100 baseline .368367/.656754/.347404/.655034 -> .103369/.899304/.103369/.899304
48000 baseline .370486/.65437/.349785/.652671 -> .108911/.89375/.108911/.89375
88200 baseline .378467/.645347/.358867/.643784 -> .131109/.8721/.130976/.872129
96000 baseline .378917/.644846/.359381/.643242 -> .132448/.870452/.132447/.870452
Gate lower weight <.30 on 8k, >.70 on 4.6k. All baseline probes fail; all candidate probes pass. Measures tracking weights, not short-event PCM attenuation or perceived sound. Existing matched-benefit Focused test rerun, all eight cases pass (target match <.02dB, air preservation >=.8dB, body <.5dB, exact unity, antiphase, partition, invalid enum, settled transition).
 
Local GCC 14.2 -O3 -Wall -Wextra -Werror and Clang17 -O1 ASan/UBSan -Werror: Focus and Dynamic targeted PASS. Exact local blobs Core 7c05b1c6329023e6d61beb587fc7209a8b16db1f, Dynamic f23b6f2ab800a55de9c8765c682478038fdd5602, rt_profile 7541337e0508bf56fd9f1a4215d07ada369720d1; existing Context and Focus hashes unchanged. Entire six-test CMake suite not run locally; scoped GitHub CI must verify it.
 
Reproducible profile added, GCC 14.2 -O3 -DNDEBUG, shared AMD EPYC 9V74, 48k stereo, 10000 blocks each after 200 warmup, same harness/seed, single run per revision. Times us:
mode/shape frames | baseline P50/P95/P99/max | candidate P50/P95/P99/max
Auto Shelf 64 | 1.943/1.983/2.094/168.114 | 1.943/1.983/2.894/118.259
Auto Shelf 128 | 3.866/3.926/6.49/488.908 | 3.866/3.916/5.368/99.581
Auto Shelf 256 | 7.712/7.792/21.903/239.641 | 7.711/7.792/21.533/246.202
Auto Focused 64 | 2.254/2.294/3.015/72.249 | 2.273/2.304/2.384/59.37
Auto Focused 128 | 4.497/4.547/8.382/192.12 | 4.527/4.587/6.47/59.139
Auto Focused 256 | 8.964/10.146/27.712/212.08 | 9.034/9.144/23.786/212.741
Voice Shelf 64 | 2.504/2.604/3.596/203.117 | 2.484/2.574/3.606/85.328
Voice Shelf 128 | 4.988/5.137/16.105/240.964 | 4.948/5.078/16.224/196.216
Voice Shelf 256 | 9.955/10.206/45.118/223.087 | 9.875/10.065/24.366/140.142
Voice Focused 64 | 2.734/2.834/4.286/187.002 | 2.734/2.805/3.916/94.623
Voice Focused 128 | 5.439/5.569/18.458/196.137 | 5.438/5.548/9.244/114.333
Voice Focused 256 | 10.857/11.106/48.994/488.578 | 10.846/11.027/27.792/230.848
No measured callback duration exceeded its nominal block period (120000 per revision); all outputs finite. Median changes within about 1%, shared host P99/max noisy; not a speedup or hard RT guarantee. Allocation instrumentation and multi-instance stress still pending. Profile executable compiled directly, CMake addition not locally verified.
 
Limits: controlled tones and seeded mixed-tone/noise timing only, no real vocal/instrument corpus or listening proof. Real corpus remains unqualified. Faster tracking may modulate rapidly on noisy evidence; measure short noise-like bursts, weak consonants, stereo spatial integrity and real program material before default promotion. No release, Windows, physical ASIO or native FX admission. Next DS-04 allocation/stress/optimization and parallel native control/UX contract; retain real audio and listener qualification as outstanding release requirement, not a blocker to isolated code development.
