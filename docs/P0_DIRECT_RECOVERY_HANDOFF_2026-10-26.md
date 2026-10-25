# P0 — Bounded DIRECT recovery and thread handoff (2026-10-26)

**State at creation:** PR from main `8a1daf5b83d02df18e3ab4dd869f5d006db5441c`. Validate current main before every write: other threads may merge unrelated GUI work.  
**Product owner:** Engine must be optimized, lightweight, reliable across diverse devices, scalable, and maintainable, with one **consolidated hardware acceptance** after backend engineering, not repeated manual device tests per small commit. No change to approved GUI mixer v0.8 unless expressly requested.

## Canonical background

- PR #19 merged P0-00 endpoint isolation and human-marked glitch diagnostics.
- PR #21 merged one-click **SIGNAL CHECK → COPY FULL REPORT**, source-labeled callback telemetry, offline clock bridge quality skeleton, and capability-based profile planner.
- PR #35 merged independently scheduled JUCE Windows Audio capture/render endpoints, bounded SPSC polyphase FIR ASRC, render-master clock, period-aware FIFO sizing, exact channel mask preservation, direct ASIO fast path, opt-in SMART CLOCK (BETA), and emergency fail-closed rollback. Merge `8a1daf5b`, Windows MSVC Release + CTest 10/10 and Chromium passed on that main.
- This slice addresses a **real gap**: if first DIRECT rollback cannot reopen a missing endpoint, `savedDirectSetup_` is retained but the earlier control timer did not automatically retry after the independent route became inactive.

## Implementation in this PR

- Pure C++20 `engine/device/DirectRestoreBackoff.h`: message-thread-only bounded schedule. First fault triggers immediate attempt, then **250 ms, 1 s, 3 s, and 8 s** retry intervals. Maximum **five total attempts**, then EXHAUSTED. Generation tokens invalidate stale callbacks/transactions; no background polling thread, blocking wait, allocation or device manipulation in realtime audio callback.
- `src/LiveAudioEngine.h` exposes `hasPendingDirectRestore()` so the original endpoint setup token is not lost after an incomplete rollback. Restoration is accepted only if the actual selected DIRECT input/output device names and explicit channel masks match, and both hardware I/O directions are active. A JUCE empty error string alone does **not** certify restored endpoints.
- `src/Main.cpp` services recovery at existing 12 Hz control timer; never reopens the experimental independent bridge automatically. On success returns to DIRECT and records the outcome. On failure, preserves fail-closed muted behavior and diagnostic evidence. Repeated failed attempts do not spawn new workers or recur forever. Manual DIRECT, AUDIO SETUP, or a new explicit SMART CLOCK transition can cancel/reinitialize policy when successful.
- Native `arMixStatus` and COPY FULL REPORT add `directRestoreState`, `directRestoreAttempts`, `directRestoreNextMs`, `directRestoreGeneration`, `directRestoreError`, `directRestorePending`. Report events `DIRECT_RESTORE_BEGIN` and `SMART_CLOCK_RECOVERY` capture recovery provenance without recording PCM or touching realtime file I/O.
- Release-active CTest covers exact virtual time/deadlines, exhaustion, success, cancellation and stale generation rejection. Node glue contract and Windows CI require the actual integration.

**Important limit:** device auto-failback to DIRECT is not the same as production AUTO/STABLE routing. This policy deliberately never retries independent capture/render automatically following a fault, avoiding Bluetooth/device flapping. It cannot make a physically removed endpoint available. Output stays muted after unrecoverable reopening failures; engineering must clearly distinguish reopened-from-unmuted status.

## Next P0 production gates

1. **Backend identity and capability orchestration:** stable Windows MMDevice endpoint IDs, actual negotiated mix rate/period, trusted shared-clock proof. Existing `AudioProfilePolicy` is tested but not yet the active platform planner; avoid guessing capabilities from display names.
2. **Quality and load:** long-duration deterministic independently clocked source/render test with burst jitter, stalls, queue wrap, sample-rate switches, alias and phase response plus p95/p99 CPU budget. Do not call host callback jitter a physical render xrun.
3. **End-to-end resiliency:** controlled profile selection, no device-operation in audio callbacks, stable last-known-good handling, output render underflow evidence from backend APIs/ETW (rather than generic JUCE heuristic), reconnection and reopen fallback.
4. **Release gate:** one consolidated user-run hardware FAT at 44.1/48 kHz on virtual capture → wired and Bluetooth output plus single-clock wired/ASIO, ZERO FX first, then VST3 effects; one-click COPY FULL REPORT from SIGNAL CHECK. Until then P0 issue #15 stays OPEN, no blanket crackling-free claim.

## CI / merging / handoff

- Build exact PR head on Windows 2022 MSVC Release, all CTest including `BoundedDirectDeviceRecovery`, native/Node UI integration, Chromium smoke. Only merge if both workflows are GREEN and review comments resolved.
- After merge, verify main CI + artifact; checkpoint commit in a dedicated branch and add issue #15 reconciliation comment.
- Existing GUI baseline is externally maintained in parallel. Do not overwrite `web/styles.css`, `web/app.js`, `AR-MIX-Live-FX-Host-Standalone-v0.3.html`, or close historic PRs indiscriminately.
- Repository `main` and this file, not chat recollection, are the handoff authority. If another thread continues, check current main/PR/actions first, then pick up from the first unfinished gate above. Avoid starting a parallel overlapping P0 architecture.
