# DS-03B — optional adaptive Focused frequency targeting
Audited PR #901 HEAD 48b04a346bb0dc3f951fbf65a44921bfa905659e, main 2253001926e6ef318eb00c9673417dc1177df4d6. DS-03A fast native and focused DSP CI success (45589107640,45589107529), required merge gate pending, no review threads. Baseline Core/Context blobs locally verified before change. No shared mixer/GUI files touched.
 
Design: FabFilter Pro-DS official advanced controls https://www.fabfilter.com/help/pro-ds/using/advancedcontrols describes split versus full-band tradeoffs; https://www.fabfilter.com/help/pro-ds/intro/about describes frequency-filtered detection and lookahead. Original causal alternative: retain Shelf default and add explicit Shape::Focused, reuse already-computed 4.6/8kHz constant-peak bandpass samples, no new filter bank. Strongest positive channel excess chooses ratio; sharpen with squared weights, normalize, smooth 8ms. Linked weights and gain across channels; dry minus weighted band residual. No coefficient modulation, no extra buffering. Separate detector policy from shape. No proprietary code copied. Candidate retains possible suppression around both broad bands; not surgical continuous frequency tracking.
 
Matched-center benefit regression (quadrature tone amplitude, controlled 800Hz body + 12kHz air tone + severe 4.6 or 8kHz tone, Shelf Amount .5 Range18, Focused Amount binary searched, Range18). Values dB:
rate | target | Shelf cut / Focused cut | Shelf air cut / Focused air cut | Focused body cut | lower weight
44100 | 4600 | 2.48918/2.48918 | 4.01120/.215827 | .0453125 | .911505
44100 | 8000 | 3.93557/3.93491 | 4.48979/1.00126 | .0226509 | .0905278
48000 | 4600 | 2.53285/2.53220 | 4.10228/.246955 | .046388 | .905859
48000 | 8000 | 3.99016/3.98945 | 4.56148/1.11346 | .0238949 | .0961625
88200 | 4600 | 2.73375/2.73384 | 4.52315/.390619 | .0510879 | .883397
88200 | 8000 | 4.38258/4.38246 | 5.06053/1.60815 | .0297639 | .118598
96000 | 4600 | 2.76237/2.76298 | 4.58357/.403062 | .051584 | .881884
96000 | 8000 | 4.41761/4.41709 | 5.10576/1.64479 | .0301837 | .120117
 
Gate >=2dB matched target, mismatch <.02dB, air benefit >=.8dB vs Shelf, body cut abs <.5dB, correct band weight, one-frame vs 127-frame exact PCM, antiphase stereo/mono parity, invalid enum reject, exact Amount0 unity, Focused->Shelf settled active PCM equals reference. Local exact Core blob 69290b81eaaf5f7bae79d8bf3aef4f87dc167b27 and focus test 174d5c937ba42a807c33ba30a25224602aaef696 compiled and passed with GCC -O3 -Wall -Wextra -Werror and Clang -O1 ASan/UBSan -Werror. Fixed initial Clang indentation diagnostic, no gate change. Context unchanged. Entire 5-test CMake suite and CLI changes not locally rerun; dedicated GitHub CI pending. No fresh callback benchmark, no allocation measurement. Reusing filters does not prove equal CPU cost.
 
Limits: synthetic stationary mixed-tone tests, not a listening result, not a real fricative or singing corpus. No external corpus downloaded (local DNS/network unavailable). 8ms smoothing may miss rapid target changes; simultaneous differing stereo sibilants and channel-order ties need qualification, transient peak/overshoot and full Voice+Focused corpus untested. Dynamic response may alter phase, and scalar Range is not a guarantee of exact spectral cut. Candidate remains opt-in; no native exposure yet. Next: real 44.1/48k+ licensed speech/singing and bright instruments with held-out annotated events, short/alternating noise-like bursts, matched-benefit blind A/B, then RT optimization and control integration. No Windows dispatch, merge, hardware or public release.
