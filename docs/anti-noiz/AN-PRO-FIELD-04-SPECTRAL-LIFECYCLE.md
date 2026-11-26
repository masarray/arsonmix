# AN-PRO-FIELD-04 — spectral lifecycle candidate

Baseline: #948 / 16cf6c47b0970cb87ef686674fbcee06def434ef. Preserve existing sample rate, FFT size, delay, and real-time CPU gate.

- Re-Learn uses the trusted spectral profile and retains the live context and mask; rejected/cancelled capture does not reset them. Automatic estimate tracking is paused during Learn.
- Newly committed PSD is exposed immediately to readback, but DSP anchor and working estimate hand over across 100 ms of existing FFT hops. First Learn retains the previous behavior. No new FFT, queue, delay, allocation, or transport schema.
- Tonal capture no longer resets or suspends the previous live tonal filter during re-Learn. Accepted tonal replacement still resets filter state and needs separate musical qualification/crossfade.
- Expanded manual spectral regression covers sample-exact live/re-Learn and cancel PCM, and monotonic bounded PSD handover at four rates and mono/stereo.

Baseline focused Chromium failure is at line 555, after rejected factory recall (not the preceding Save As assertion). The mock IPC had not serviced a fresh snapshot after the factory ACK. Candidate fixture supplies matching snapshot replies and still asserts saved user selection before/after rejection; production freshness guard and shared preset logic unchanged. Root-cause hypothesis pending exact CI. Automatic and tonal replacement PCM, sidechain reference lifecycle, callback-stop, auditory FAT and installer remain open. Source candidate only until exact CI passes; no main/release action.
