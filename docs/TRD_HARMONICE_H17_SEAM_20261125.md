# H17 — slope-aware renderer seam and transition evidence
Baseline 2edc7e717034d538e0de9aed3fd140977182c498, PR #889. H16 passed exact DSP and fast native, Windows FAT 45484221747 in progress at audit. H16 isolated fallback fix strong, full engine maximum adjacent derivative improvements only 0.04–0.24%. No real vocal PCM or installed SHA confirmed.
## Design
Existing residual seam freezes its first derivative on loss/acquisition. Match outgoing residual slope at first sample with compact polynomial k*(1-k/N)^2, normalized at k=1, N=0.5ms; cap tangent using recent 10ms peak-decay envelope. Preserve 3ms base crossfade and 1ms reacquisition warmup. No delay, FFT, new audio buffer, thread, state format, GUI or dry/lead/Doubler change. Preserve exact historical H12–H16 offline references. Per-role audio-owned loss/acquisition counts read offline; not real-time GUI telemetry.
## Evidence requirements
96 forced loss cases: 4 rates, 3 frequencies, 8 phases, >90% reduction of summed boundary slope mismatch with no unbounded peak, exact steady PCM, endpoints, chatter and reset tests. Eight full engine per-ear matched A/B cases, exact dry and block parity; max normalized edge <=1.15 and p99.9 <=1.10, wet gain within 0.5 dB. Event counts establish whether seam is exercised; adjacent derivatives are not a validated crackle detector. Require all previous gates. No acoustic or CPU improvement claims without measurement.
## References
- Steinberg WaveLab zero crossing: https://www.steinberg.help/r/wavelab-pro/13.0/en/wavelab/topics/audio_files_editing/zero_crossing_c.html
- Fink et al., DAFx 2016 signal-matched cross-fading: https://www.dafx.de/paper-archive/details/gUO-U6xEDk0Z_m4S-PivJQ
- Audacity fade/crossfade: https://manual.audacityteam.org/man/fade_and_crossfade.html
No duplicate Windows FAT while H16 active; real vocal A/B and ASIO hardware qualification remain release blockers.
