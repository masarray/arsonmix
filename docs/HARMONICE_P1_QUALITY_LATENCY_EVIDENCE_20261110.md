# Harmonice P1 — latency/quality engineering evidence, 2026-11-10

**Status: experimental; NOT PRODUCTION APPROVED.** Shipping native `ChannelHarmonyProcessor` calls only `Engine::prepare(rate)`, retaining the approved **32 ms algorithmic latency**. Do not advertise a new live latency, guaranteed acoustic naturalness or physical ASIO XRUN result.

## Checkpoints / independent ownership

- Baseline main `6bcf88af30629d2e20026bfa8e0a84163c06c29e`.
- Reliability parent [#509](https://github.com/masarray/arsonmix/pull/509) at `618cb67c476ffe41904175cc87890f075c9c3b21`, exact SHA Windows [run 42735079669](https://github.com/masarray/arsonmix/actions/runs/42735079669): 80/80 CTest PASS, FAT/Inno preview artifacts. 8 ms sample-rate-correct bypass and one-shot Music guide detector reset verified at that specific SHA.
- This experiment [#514](https://github.com/masarray/arsonmix/pull/514), stacked over #509, **isolates offline-only 24/16 ms preparations**. Neither mixer/device ownership nor live plugin default PDC is changed.
- Rejected CPU cursor experiment [#515](https://github.com/masarray/arsonmix/pull/515): exact same PCM but inconsistent callback tails and worse measured p50/p95; closed **unmerged**.
- Shared owner PR [#512](https://github.com/masarray/arsonmix/pull/512) overlaps #509's JUCE processor and bypass/music tests. Do **not** blindly merge #509 or its stack with #512; reconcile on current main only when the shared mixer owner is ready. Vocal Throw #507 remains separate and owner-listening blocked.

## Real latency, fixed PDC and causal PSOLA limits

Granular algorithmic delay = two 16 ms grains. Candidate delay is still **twice the actual grain length**, rounded to device-rate frames: no underreporting to JUCE/PDC. Synthetic native PCM fixture verifies Wet0 exact delayed impulse, bounded wet output, opposite-polarity stereo, stable output for callback partitions 64/127/256, at 44.1/48/96 kHz. Actual sample counts:

| Rate | 32 ms reference | 24 ms study | 16 ms study |
|---|---:|---:|---:|
| 44.1 kHz | 1412 | 1058 | 706 |
| 48 kHz | 1536 | 1152 | 768 |
| 96 kHz | 3072 | 2304 | 1536 |

**Important negative quality result, based on DSP source physics and actual shifter PCM:** `HarmonyPitchSynchronous` requires `fixedDelay >= 1.2 * sourcePeriod`. At **65 Hz**, the causal minimum is **18.46 ms**. Consequently **16 ms cannot use the pitch-synchronous path at 65 Hz**; the real shifter returns not-ready and the engine falls back to its earlier granular shifter. That fallback is an existing intended safety mechanism, but not evidence of formant-stable natural backing singers. **16 ms is rejected as a full-range quality candidate** under the current architecture. At 65 Hz 24 ms and 32 ms do produce pitch-synchronous output, proven at 44.1/48/96 kHz; 82/110/220 Hz controls also checked with 64/127 partition identity. This does *not* mean 24 ms is automatically acoustically acceptable; it merely clears one physical feasibility gate.

Candidate 24 ms reduces nominal algorithmic latency by **8 ms**, but no live runtime switch exists. True round-trip latency additionally includes ASIO buffers, input/output converters, device and other FX graph/PDC; it has **not been measured**.

## Reproducible PCM and listening instrumentation

Compile the existing native console tool, then run:

```bash
# Optional reproducible DIAGNOSTIC samples (not human singing)
ARMixHarmonyOfflineAb --synthetic-corpus /tmp/harmonice-synthetic
# Human listening source; keep original singer recordings private
ARMixHarmonyOfflineAb --latency-study path/to/original-vocal.wav /tmp/harmonice-listen
```

Accepted source: original mono/stereo PCM16/PCM24 or IEEE float32 WAV, 32–192 kHz, <=60 sec. The second command writes a reference 32 ms wet mix/dry lead and 24/16 ms candidate wet mix/dry lead **time-aligned to 32 ms**, plus `latency-study.json` with actual PDC frames, alignment offsets, source-to-source RMS difference, backing-only RMS, stereo-side/mono-fold RMS, peak sample, largest adjacent-sample step, and 65 Hz causal support. These are **diagnostics, not perceptual naturalness scores**. The WAVs are not loudness-matched; level-match separately and blind-randomize before judging sound.

The synthetic corpus includes low C2 (65 Hz), A3 (220 Hz), A4 (440 Hz), opposite-phase stereo A3, deterministically generated vowel-like harmonics, syllable onset, release and breath-like noise. Every fixture, WAV container and JSON report has an executable Python contract. There are **no original adult singer recordings in this repository** and no acoustic certification implied.

## CPU: same-runner synthetic DSP evidence, non-gating

Previous [Linux benchmark run 42752957451](https://github.com/masarray/arsonmix/actions/runs/42752957451), five backing voices, live Music key detector, 64 samples, Wet 50%, Space 60%, sustained synthetic A3:

| Rate | 32 ms p99.9 | 24 ms p99.9 | 16 ms p99.9 |
|---|---:|---:|---:|
| 48 kHz | 31.24 µs | 26.42 µs | 27.04 µs |
| 96 kHz | 23.82 µs | 27.40 µs | 25.02 µs |

No consistent synthetic CPU win or p99.9 promise; host/machine noise and lack of Windows ASIO integration make this an exploratory measurement only.

## Decision / next acceptance gates

1. **Retain 32 ms production PDC and shipping sound unchanged.** 16 ms not allowed to ship as full-range candidate.
2. 24 ms is **research-only**, pending human adult singer original recordings from low bass through soprano, consonant attacks, vibrato and stereo anti-phase, perceptually level-matched and aligned blind A/B against the exact 32 ms reference SHA; reject on metallic artifacts, formant deviation, unstable notes, stereo fold and altered room tails.
3. Windows full MSVC+CTest at **the final exact candidate SHA** plus FAT provenance if a preview is needed. PR draft quick CI is not proof of the Windows full job. Keep expensive workflows selective.
4. Physical 30-min Windows ASIO singer session with 64/128/256, 48/96 kHz where supported, guide drop/reacquisition, rack/slot bypass, device loss/restart, WIRE/SOURCE ONLY, .smix, Compressor/Player. Measure actual round-trip latency and XRUNs.
5. Neither #514 nor any QA mirror may be merged/published as stable while these gates remain open. Do not bring Vocal Throw into this task.

**Quality verdict:** exact dry timing and deterministic synthetic bounds are proven; low-note 16 ms physics disqualifies it; 24 ms remains a plausible, *unqualified* listening candidate; real-singer naturalness and Windows ASIO performance remain **NOT VERIFIED**.
