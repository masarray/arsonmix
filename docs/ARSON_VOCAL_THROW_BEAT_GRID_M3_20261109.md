# ArSon Vocal Throw M3 — native music beat-phase quantized echo

**Branch:** `feature/arson-vocal-throw-phase-grid-m3-20261109`; builds on draft M2 PR #474 (which builds on M1 PR #469). Product issue #473. **Not a hardware-certified stage release.**

## What changed

A stable BPM alone cannot locate the *beat boundary*. M3 shares the existing native ChannelId audio renderer's monotonically increasing callback frame counter with its built-in FX processing, then quantizes **only the newly scheduled echo** when the selected MUSIC ChannelId has a credible beat-phase estimate.

```
ONE native ASIO/JUCE ChannelId render (epoch + graph revision)
  beginAudioBlock(frames) -> monotonic native sample-frame start
  NativeBeatFrameScope (thread-local, callback-only and restored on exit)
  pre-FX MUSIC ChannelId -> 10ms nonmutating energy frames
    each summary carries its exact observed audio end-frame (within 10ms)
    SPSC -> existing control timer -> BPM, onset anchor, phase confidence
    CONTROL publishes BPM + sample-frame anchor + epoch/revision to FX
      -> atomic version-fenced snapshot (one read attempt per callback)
  VOICE FX on SAME callback/thread
    -> read versioned tempo + scoped native frame origin
    -> if source/epoch/revision invalid: accepted Manual/TAP
    -> if BPM stable, beat phase weak: BPM-derived interval, NO grid
    -> if BPM and phase credible: snapshot anchor & interval at THROW
       -> freeze voice with existing two-tail bounded PCM
       -> schedule next echo at FIRST native music grid point >=
          intended (phrase end + musical subdivision), with correction
          bounded to ONE subdivision and zero change to dry PCM
    -> already sounding echo tails retain frozen timing across BPM/graph edits
```

### Realtime invariant
No new audio callback, dynamic allocation, model inference, FFT, lock, waiting, JS audio, logging or file I/O from the callback. Channel PCM enters one mixer graph and is never processed twice. The timing scope is a trivially copied thread-local struct, no global audio clock thread. Tempo/phase inference (up to 121 BPM candidates) is CONTROL-only. Packed atomic scalar fields are protected by an even/odd version; the audio callback makes **one** bounded read attempt and immediately falls back on inconsistency.

### Confidence and fallback
- BPM confidence and beat-phase confidence are independent. A musical onset beat grid is used only if a majority of percussive onset strength agrees on a single pulse lattice.
- Ambiguous rhythmic patterns remain **AUTO BPM · BPM only**, not a speculative grid. A missing source or stale graph becomes **Manual / TAP** without losing the accepted preset.
- A 10ms analysis window means onset time has approximately up to one window of inherent beat-position uncertainty, even though *subsequent scheduling* uses exact callback sample indices. This is not sample-perfect transient detection.
- Phase quantization is measured against **pre-FX** selected music PCM, not against a fully latency-compensated external performance clock. Vendor VST3 latency, off-beat percussion, compressed music, drift, sample-rate changes, ASIO callback reentrancy and sparse backing must still pass field tests.

### Automated acceptance
- Native DSP unit test verifies an actual frozen phrase's first echo is scheduled on the music frame lattice and remains there after transport disappears, compared with Manual/TAP quarter-note fallback; repeats at 64/128-frame PCM chunks with callback heap forbidden.
- Native graph PCM test stamps the selected music ChannelId from one authoritative callback at variable 64+36 sample chunks, verifies anchored BPM/phase confidence at 72/95/120/160 BPM, one block-clock scope, frame counter continuity after a graph revision, and restoration of callback-only context.
- Desktop/mobile Chromium smoke explicitly distinguishes a native ACKed source's **BPM only** from **BEAT GRID** status. No user-facing opt-in or extra monitor/arm step.

### Deferred release qualification
Physical ASIO 64/128 frame loops at 44.1/48/96 kHz; on-stage singer/backing leakage, syncopated 4/4 and half/double-time, large FX PDC, dropout/replug, fade/no-click audition, real CPU/xrun vs bypass, and sustained stress still require field evidence. Do not merge while the PR chain and these safety gates remain unresolved.
