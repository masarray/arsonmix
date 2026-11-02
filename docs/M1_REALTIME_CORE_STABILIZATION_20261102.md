# M1 — Realtime core stabilization (2026-11-02)

**Stacked source**: ENGINE #266 at `40a059785993cc6a4bb593020bf9749de92746f9`. No competing DSP, native callback, GUI or worker per channel.

This checkpoint repairs the Windows CTest AUX static/offline regression and corrects native fader/PAN de-zippering to remain **64 audio samples across variable callback frames** (4/16/32/12), including reverse direction. Existing live PRE/POST sends and AUX return faders retain their prepared 64-sample ramp; offline/no-live paths must not inherit old live AUX targets. All work uses fixed-size arrays and no new callback heap allocations.

The existing `RealtimeRoutePublisher` owns bounded retirement. New tests hold a real leased reader across two graph publications, verify refusal when the queue is full, and verify all four prepared owners are reclaimed after the reader leaves. All tests run in the existing CTest executables with no additional CI matrix.

**Runtime invariant:** `LiveAudioEngine::audioDeviceIOCallbackWithContext` is still M4 until the full CH1 external/Orbit owner, native vendor FX/editor/Show, physical input/output pins and monitor can migrate together. Never enable universal supportedOps or silently render the new graph alongside M4. Worker-only work belongs to existing control/Show-save processing, NOT the real-time callback.

**Remaining M1 release gates:** pass exact-SHA Windows MSVC/CTest, integrate a single full-fidelity ChannelId callback with atomic admission and no FX loss, then qualify ASIO/WASAPI and zero-FX crackle on actual hardware. This change alone is **not M1 complete**, not UAT, not acoustic FAT.

## Worker payload lifetime hardening
`NativeShowSaveWorker` validates Show IDs before reserving, releases snapshot/blob/closure memory immediately on start failure, and moves the completion closure rather than retaining a second copy after IO completion. The long-lived worker is reused; no new OS thread or realtime allocation has been introduced. Windows JUCE compilation and runtime verification are release gates.
