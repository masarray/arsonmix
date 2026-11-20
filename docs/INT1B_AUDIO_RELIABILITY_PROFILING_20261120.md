# INT-1B — real audio-reliability evidence, no speculative DSP refactor

**Repository:** [ArSonMix](https://github.com/masarray/arsonmix) · parent [INT-1 #837](https://github.com/masarray/arsonmix/issues/837)  
**Scope:** existing single-primary-ASIO native PCM/VST3 graph and approved `/preview/`. No multi-device ASIO, graphical overhaul, changed buffer or auto-tuned audio driver.

## What actually changed

1. The existing `LiveAudioEngine::status()` publishes the 13 already-accumulated `RealtimeHealthHistogram` bucket counts, with exact provenance. The audio callback keeps precisely its existing relaxed-atomic histogram update; **no allocations, locks, new timers, IO, COM, driver opens or new render graph**.
2. The existing 1 Hz `AUDIO_HEALTH` log now writes a compact, versioned **parseable JSON subset** of the status already fetched by the message thread. The earlier full status can include long FX/device arrays and exceed the 4096-byte async queue entry. The small health record avoids silent JSON truncation without reducing the original interactive SIGNAL CHECK / COPY FULL REPORT data. The log itself stays on the existing asynchronous worker, is local/private and rotates at 2 MB.
3. `scripts/audio_reliability_profile.mjs` compares **changes between consecutive 1 Hz histogram snapshots**. It outputs per-window p95/p99 **callback deadline bucket ceilings**, over-budget per 1,000 callbacks, late scheduling indicators and device-generation changes. It splits samples whenever a device/route/driver/rate/buffer/mode changes, counters reset, the callback disconnects or the recording has a gap over 5 s. It refuses a DRY/FX comparison when hardware parameters differ. Raw buckets permit real *interval* percentiles rather than a stale lifetime average.
4. Portable regression contracts cover malformed/truncated/legacy records, true 13-bucket delta percentiles, >100% deadline, **actual accepted insert/rack/global bypass classification**, matching dry vs FX, device epoch changes, counter resets and unknown hardware XRUN status. Expensive CI is only required once for the final native candidate, not for each test change.

## Operator procedure — collect once on an exact Windows build

Use the new INT-1B installer/portable **only when an exact-SHA Windows FAT artifact exists**. Save your existing `.smix` first. ASIO interface and physical output remain chosen explicitly by you. Start at low headphone volume, never auto-arm speakers.

1. In the approved native application, open **SIGNAL CHECK**, verify driver, exact input/output pins, rate and buffer. Keep the same ASIO device (for example, 48 kHz / 256 frames) and an accepted CH→MAIN routing.
2. Use the **native GLOBAL FX POWER/BYPASS control** (wait for exact accepted ACK/readback) to bypass all FX for at least **2 minutes**, without a route/device change. The old `dryTest` flag is only UI intent and is **not reliable evidence** that the accepted ChannelId DSP is bypassed. Keep the audio signal continuous. `AUDIO_HEALTH` snapshots are collected automatically at the existing 1 Hz rate without any extra GUI telemetry activity.
3. Enable GLOBAL FX POWER and verify at least one installed native FX insert is **actually processing** (accepted rack/slot/global bypass readback). Keep **identical** driver/device/rate/buffer and monitor level, and record at least **2 minutes**. Mark any audible crackle using **MARK CRACKLE**. Do not conflate manual silence with a dropout.
4. In SIGNAL CHECK choose **OPEN DIAGNOSTIC LOG**. The local `audio-diagnostics.log` contains records. It is **not uploaded** by ArSonMix. On a development machine with Node 20+ and the ArSonMix source, run:

```powershell
node scripts/audio_reliability_profile.mjs "C:\path\to\audio-diagnostics.log" --json int1b-profile.json --markdown int1b-profile.md
```

5. Review private device names or FX details before sharing the Markdown/JSON evidence. If the log rotated, analyze each part separately and compare equivalent driver/rate/buffer conditions. Only a single window with >=30 seconds and accepted physical routing is considered minimally comparable; **120 s per mode recommended** for reliability evidence. Never claim long-run stability from 30 seconds.

## How to interpret accurately

- **p95/p99** are *upper limits of coarse callback-processing deadline buckets* based on cumulative-count **deltas**, not exact processing-time percentiles or OS CPU percentage. The >100% bucket remains explicitly unknown/unbounded.
- **Over-budget** means measured callback processing time exceeded its actual block-period budget. **Late callback / gap** refers to START-to-START cadence relative to the *previous* variable JUCE quantum. Both are **indicators**, not proof of an actual render-underrun.
- `backendXrunMeaning=WASAPI_CAPTURE_DISCONTINUITIES_ONLY` is specifically **capture flags**, not DAC starvation. `DRIVER_DEFINED` has driver-dependent semantics. True render-hardware XRUN remains **UNAVAILABLE** through the currently pinned JUCE AudioIODevice API.
- `worstPluginUs` is a **cumulative maximum**, not necessarily measured inside the reported window. No automated causal plugin verdict based solely on that value.
- **Verified DRY vs FX:** DRY means accepted native processing-insert count `0`, either by global bypass or no inserts. FX means processing-insert count >0, after native ACK/readback; the legacy `dryTest` flag cannot establish either. Device generation, exact hexadecimal ASIO input/output pin masks, native graph revision, input/output names and channel counts must all match. A mere identical driver name is NOT sufficient. Compare p99 bucket ceiling, over-budget/1000, cadence and audible observations together. Only after an actual regression is observed should the responsible DSP/FX/device/GUI subsystem be optimized with a targeted patch and before/after proof.
- Profiling cannot measure acoustic round-trip latency, DPC/ISR or hard OS page faults; those require independent synchronized hardware/Windows tools. Existing [CrackleEvidenceWindow](../engine/diagnostics/CrackleEvidenceWindow.h) and [Windows GUI sampling](../scripts/collect_gui_perf_windows.ps1) remain separate instruments.

## Exit gates

- Windows MSVC Release + native CTest + quick source contracts at exact SHA, controlled release artifact installation/upgrade/uninstall **when a user-test binary is required**.
- Read `audio-diagnostics.log` from that exact user-installed binary; assert parseable profile counts, no log truncation and retained device/session/provenance.
- **Physical hardware FAT** (ASIO driver model, hardware, listening, glitch markers, loaded VST3 chain) must be reported PASS/FAIL/NOT RUN. Never infer physical FAT from CI.
- I/O #825/#830 and Graphic EQ #835 remain distinct experiments; no dual-ASIO UI claim.

**Measured source-workload win:** one existing native status read and 1 Hz asynchronous log write are reused, instead of adding a callback probe, UI polling timer or another audio thread. Without real hardware evidence this does not justify a numerical reduction in CPU, latency or XRUN.
