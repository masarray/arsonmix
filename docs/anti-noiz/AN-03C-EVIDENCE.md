# AN-03C — useful suppression after Learn
Owner correction: excessive conservatism in ASKP-VST is unacceptable. Parent f06bc1e68a70cd262ff4e42bb6f9d6f7580eb520, previous focused and fast native CI PASS.
## Cause and decision
Explicit Voice used sqrt of a Wiener-derived gain; applying the gain directly as amplitude gives greater attenuation in noise-dominant bins, with no added state, FFT, lookahead or dependency. Keep decision-directed estimator and high-SNR onset blend. Do not increase default Clean blindly. Evaluated temporal smoothing alternative: mixed proxy error improvement regressed to 2.02462 dB, below existing 3 dB gate; rejected. Another persistent evidence cap improved tone proxy but slightly regressed mixed proxy against simple amplitude gain; rejected extra complexity. Auto remains unity on foreground and must not be advertised as a fully automatic active denoiser. Source-aware mic policy and musical Auto still require implementation/real audio validation.
## Evidence
Hash verified against local test files: gain 5ed4bac4982f2be7ef2cc5ba9fd8a7ddc1d30d12, benefit test a00c483e07df76aafc3e3d0bf67486e3eeb70eae, CMake 5a3f0704e2be286a203c414c6429157c6a4513fb. GCC Release 7/7 CTest PASS 0.75s, Clang ASan/UBSan 7/7 PASS 2.08s.
New fixture: continuous harmonics with independent first-difference uniform hiss, separate learned reference, 48k, Silence=0, compare to known clean signal after first second. Error improvement includes distortion and residual noise.
| Case | Old dB | New dB | New desired projection |
|---|---:|---:|---:|
| Mono Clean 14 |8.38493|10.206|.987923|
| Mono Clean 24 |8.9325|11.257|.986815|
| Stereo Clean 14 |7.84426|10.5919|.98909|
| Stereo Clean 24 |8.16011|11.8552|.988353|
Clean 0 unity delta RMS <1e-7. New gates reject previous implementation on same input. Existing mixed harmonic/fricative proxy improvement 3.62388 -> 4.15152 dB, projection .989475, onset .997993, mixed fricative interval .988407; existing single tone proxy improvement 3.20047 -> 5.10807 dB, projection 1.00002. Existing idle 36.17 dB, all prior tests pass. No real speech/music corpus, active low-SNR quality, CPU P99 qualification, native bridge or ASIO FAT yet. No full Windows dispatch. This does not establish 10 dB reduction on every real-world voice or music source.
