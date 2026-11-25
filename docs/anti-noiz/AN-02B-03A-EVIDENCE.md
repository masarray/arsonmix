# AN-02B/03A — bounded musical protection and selective Voice gain
Parent before this milestone: 0f13fe47fd332d86c256b5d184c3bb64ad713557. AN-02A offline renderer and flat/isolated GUI asset correction retained. Prior focused DSP/Web CI and fast native CI passed. No full Windows job dispatched.
## Engineering rationale
Auto pause detection based only on frame total/group power could occasionally suppress a weak narrow-band foreground buried in broadband noise. ContextGuard adds causal 45 ms averaged adjacent-bin evidence on the existing spectrum, separately per channel; either channel protects both. Cap temporal evidence at 8x profile to bound recovery after loud foreground; immediate broadband/group guard is unchanged in principle, 180 ms hold remains. The uncapped initial candidate failed existing independent-stereo idle regression; bounded evidence fixed recovery without weakening the gate. Fixed storage, no extra FFT, no lookahead, no per-bin exp/pow. This is foreground protection, NOT speech/singing/music classification, and low-level onset may still be attenuated before evidence accumulates.
VoiceGain is a separate causal decision-directed Wiener-inspired estimator (25 ms sample-rate-derived memory, bounded posterior, internal unfloored estimate, high-SNR instantaneous blend). Enabled only for explicit Voice while non-idle. Auto/Singing/Music do not use it. It trades suppression against distortion; actual listening and low-SNR consonant/tail tests remain mandatory. No ASKP-VST code/dependency. References: Cohen, On the Decision-Directed Estimation Approach of Ephraim and Malah, ICASSP 2004 (https://webee.technion.ac.il/Sites/People/IsraelCohen/Publications/icassp04.pdf); Sixty Years of Frequency-Domain Monaural Speech Enhancement (https://pmc.ncbi.nlm.nih.gov/articles/PMC10658184/); An adaptive a priori SNR estimator for perceptual speech enhancement (https://link.springer.com/article/10.1186/s13636-019-0150-3). These explain the smoothing/onset tradeoff; do not claim exact paper implementation or perceptual equivalence.
## Reproducible results
Local GCC Release 5/5 CTest PASS, 0.63 s; Clang ASan/UBSan 5/5 PASS, 2.07 s. Exact code blobs verified before commit.
| Synthetic fixture | Baseline | Candidate |
|---|---:|---:|
| Weak tone relative projection, 44.1 kHz | 0.987706 | 1.0 |
| Weak tone relative projection, 48 kHz | 0.971172 | 1.0 |
| Weak tone relative projection, 88.2/96 kHz | 1.0 | 1.0 |
| Independent stereo idle RMS reduction (offline prior fixture) | 35.6213 dB | 35.3862 dB |
| Existing tone proxy active error improvement | 1.81287 dB | 3.19894 dB |
| Mixed harmonics + high-band burst proxy error improvement | 2.22448 dB | 3.62438 dB |
| Mixed proxy desired signal projection | 0.994918 | 0.991981 |
| Mixed proxy first 10 ms projection | 0.998306 | 0.999097 |
| Mixed fricative interval projection | 0.99454 | 0.991642 |
Context fixture uses independently generated stereo noise and a weak 750 Hz tone only on right; steady projection relative to noisy input, not onset or real music. Initial/final idle suppression >=20 dB, observed 31.61–35.71 dB; exact partition parity 64 vs 127 samples. Active fixture uses known clean reference; error improvement is 10log10(input error power/output error power), includes both desired distortion and residual noise, not measured speech quality. Projection is not a guarantee of perceptual preservation. New gates 0.995–1.005 steady projection and >=3 dB mixed proxy error improvement distinguish the old implementation on these deterministic fixtures; retain previous tests. No extra latency; existing 256/512 samples delay (5.33–5.80 ms). CPU P99/max, allocations instrumentation, real speech/music corpus, low latency target, native bridge, Windows/MSVC/ASIO and installer still pending. No release or full Windows dispatch.
## Next
Add real labeled/licensed corpus (including weak consonants, reverberant singing, stereo music and nonstationary noise), segment-level onset and tail metrics, callback budget profiling, then native integration. Do not automatically enable active Voice processing in Auto until corpus supports it. No user action needed yet.
