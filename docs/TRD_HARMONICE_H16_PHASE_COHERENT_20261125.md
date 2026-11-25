# H16 — phase-coherent fallback carrier
Baseline a721cab77341fdf11121b4626fdca8aec253fd19, PR #889. H15 exact scoped CI 45482879311 passed; epoch jumps 5→0 and contour retention .602–.611. However H15 max adjacent PCM derivative normalized vs H14 ranges .904–1.216 across 8 channels/fixtures; six increased. User reports sound improving but intermittent crackle persists. No actual vocal PCM or installed SHA confirmed.
## Root cause candidate
The legacy two-tap carrier computes delay = base+(1-ratio)*phase*grain. A moving ratio adds an unintended delay derivative proportional to grain age. Fixed 16 ms geometry may place the two taps half a source cycle apart and cancel periodic vocal energy at the middle of a crossfade. This can become audible when PSOLA yields to fallback. Does not prove that this is the user's sole cause.
## Design
Integrate delay velocity per sample; reset each read head only at its cubic window zero crossing. When trusted F0 exists, select a whole-period offset relative to the audible head, near the nominal delay and within conservative full-grain history bounds. No extra PCM buffer, FFT, PDC, schema, GUI, lead Tune or pure Doubler change. Mono Duo fallback also benefits. Preserve H15 flag and historical offline WAV references. Real-time cost: fixed state/two adds per voice plus alignment only at zero crossings; no speed claim before benchmark. Correlation trust is not perfect; untrusted periods use unaligned nominal reset.
## Evidence
Direct analytic half-cycle cancellation (125 Hz, ratio 1.5), 32/48/96/192 kHz, glide velocity, ring bounds and gap reset; matched 3/5 singer stereo 48/96k, block and dry parity and gain-normalized edge diagnostics. No weakening of existing gates. Full regression and real vocals required before release.
## References
- DAFX time-segment processing: https://www.dafx.de/DAFX_Book_Page/chapter7.html
- Driedger and Müller (2016), waveform similarity overlap-add and phase discontinuities: https://www.mdpi.com/2076-3417/6/2/57
- PortAudio callback underflow and overflow: https://portaudio.com/docs/v19-doxydocs/portaudio_8h.html
