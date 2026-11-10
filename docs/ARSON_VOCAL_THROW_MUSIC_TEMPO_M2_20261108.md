# ArSon Vocal Throw M2 — Native Music Channel Tempo Detection

**Status:** implementation and qualification draft, stacked on Vocal Throw M1 PR #469. M1 green head `4db384851e6f6378cca4fd037b744c98bd1f0ba3`; M2 is PR #474 and tracks issue #473.

## Signal authority

```
Authoritative ChannelId native PCM graph, single JUCE / ASIO callback
  MUSIC ChannelId selected explicitly by singer/operator
    -> PRE-FX non-mutating read-only music tap
    -> 10ms stereo absolute-energy summary, bounded SPSC<Frame,2048>
    -> existing 12Hz JUCE control timer: onset search + BPM confidence
    -> accepted tempo precomputed into sample intervals, CONTROL only
    -> per-VocalThrow atomic pending interval
    -> next dry/vocal PCM callback copies interval BEFORE any THROW
    -> new frozen vocal echo gets music interval, old tails keep prior schedule
  VOICE ChannelId -> VocalThrow (no added dry PCM delay) -> WIRE/BUS/MAIN
```

**AUDIO callback contract:** no memory allocations, FFT, file I/O, logging, locks, networking, JS DSP, or new audio threads. Only the explicitly selected source channel's input stereo samples are summarized; every other channel skips the per-sample music tap. Existing JUCE callback remains the only audio authority. Core audio buffers and retained FX leases remain with the native graph.

**CONTROL contract:** only an accepted `ChannelId` with input / GROUP / FX_RETURN role and an actually scheduled PCM lane is a permissible tempo source. The native FX editor validates both its own vocal insert instance and source identity, device epoch and graph revision. Monitor/MAIN and the vocal channel itself are not accepted. On a valid structural graph recompile, an existing source ID is rebound with a new analysis generation; if absent or stale, the music clock is disabled, and Vocal Throw falls back to its saved BPM or TAP tempo.

## Detector

- A 10ms energy onset, 80ms transient refractory and adaptive floor reject a simple repeated rise from a sustained note.
- Bounded pairwise onset-period candidate scoring (60–180 BPM), with penalty for octave half/double candidates, three consistent estimates and confidence threshold.
- Native readback distinguishes **AUTO BPM locked**, **Listening**, and **Manual/TAP fallback**. A silence period exceeding 3.2s resets lock.
- UI conveys confidence as measured by estimator; this number is not field-certified reliability.

**Explicit limitations:** Music/onset BPM estimation does not provide sample-perfect beat-phase grid alignment. A recent transient anchor is retained for diagnostics only; it is not used as a globally compensated sample clock. Syncopated and heavily compressed music, live mic bleed, time-signature changes, half/double-time ambiguity and device-specific ASIO timing remain hardware field gates. Tempo selection is ephemeral song-wide, not yet persisted per Show. The fallback path always remains audible on the singer's dry stream.

## Qualification evidence

- Prior M1 Windows MSVC/JUCE: 63/63 CTest on exact SHA `4db384851e6f6378cca4fd037b744c98bd1f0ba3`.
- M2 Windows MSVC/JUCE: 64/64 CTest, including `NativeMusicTempoSourceOnsetsAndFallback`, on exact SHA `cce63231da52f69e6789873ecc406adba69aa1cb`. See [Actions run 42498186748](https://github.com/masarray/arsonmix/actions/runs/42498186748).
- Chromium FAT M2 native Vocal Throw picker, music source ACK, BPM readback and stale instance handling passed on same SHA.
- Later hardening: inbound tempo sampled before same-callback THROW and reset on plugin audio reprepare; corresponding regression checks for manual fallback and active tail interval stability. Qualification on the later SHA must be checked independently.
- Deterministic PCM: 72, 95, 120, 160 BPM; non-vocal source, stale revision, source change, dropout, default fallback. The CTest calls the *actual* native publisher to verify a music signal traverses the accepted graph, not only the isolated estimator.

**Not qualified:** physical ASIO 64/128 frame load at 44.1/48/96 kHz; prolonged simultaneous singer/backing test; live beat-grid sync / stereo echo hearing test; WASAPI secondary stream callback load; long-run xrun/restart and voice-music bleed reliability. Green CI is necessary but not sufficient for release.

## Next DSP/UX milestone

Prove stable beat phase with a source-specific latency-compensated anchor; handle half/double-time ambiguity with confidence floor and long-window hysteresis; permit song/Show fallback preference; add footswitch mapping and live capture metrics without callback work. Until then label UI as estimated **AUTO BPM** rather than **sample-accurate beat quantization**.
