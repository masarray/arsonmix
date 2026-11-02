# M1 — Realtime core stabilization (2026-11-02)

**Stacked source**: ENGINE #266 at `40a059785993cc6a4bb593020bf9749de92746f9`. No competing DSP, native callback, GUI or worker per channel.

This checkpoint repairs the Windows CTest AUX static/offline regression and corrects native fader/PAN de-zippering to remain **64 audio samples across variable callback frames** (4/16/32/12), including reverse direction. Existing live PRE/POST sends and AUX return faders retain their prepared 64-sample ramp; offline/no-live paths must not inherit old live AUX targets. All work uses fixed-size arrays and no new callback heap allocations.

The existing `RealtimeRoutePublisher` owns bounded retirement. New tests hold a real leased reader across two graph publications, verify refusal when the queue is full, and verify all four prepared owners are reclaimed after the reader leaves. All tests run in the existing CTest executables with no additional CI matrix.

**Runtime invariant:** `LiveAudioEngine::audioDeviceIOCallbackWithContext` is still M4 until the full CH1 external/Orbit owner, native vendor FX/editor/Show, physical input/output pins and monitor can migrate together. Never enable universal supportedOps or silently render the new graph alongside M4. Worker-only work belongs to existing control/Show-save processing, NOT the real-time callback.

**Remaining M1 release gates:** pass exact-SHA Windows MSVC/CTest, integrate a single full-fidelity ChannelId callback with atomic admission and no FX loss, then qualify ASIO/WASAPI and zero-FX crackle on actual hardware. This change alone is **not M1 complete**, not UAT, not acoustic FAT.

## Worker payload lifetime hardening
`NativeShowSaveWorker` validates Show IDs before reserving, releases snapshot/blob/closure memory immediately on start failure, and moves the completion closure rather than retaining a second copy after IO completion. The long-lived worker is reused; no new OS thread or realtime allocation has been introduced. Windows JUCE compilation and runtime verification are release gates.

## Exact CI evidence and hardware artifact scope (revision 2)
Windows run `41479462759` compiled successfully but 52/54 CTests passed. The remaining two failures were analyzed and corrected together:
1. Native `setAuxSendGain` / `setAuxBusGain` is **one atomic automation plane** even without separate strip controls, as exercised by `dedicated_aux_bus_test.cpp`. The old conditional gate silenced actual physical AUX level edits. In the routing CTest, a newly built `PreparedMixerBlock` establishes fresh compiled defaults, while an already automated block retains its explicit target.
2. The graph-retirement test needed to count the rejected candidate's destructor too: 3 original accepted + 1 rejected + 1 newly accepted = **5 distinct prepared instances**. The lifetime test checks that saturation safely rejects and reader-owned graphs are not reclaimed early.

This patch also authorizes **one** exact-branch `M1-EXPERIMENTAL` GitHub Actions artifact **only after** all CTests and package/provenance validation pass. This does not enable universal routing/CH11, promote stable release, or change hardware patch/ARM policy. The packaged FAT guide now includes explicit pass/fail cases for wired 0FX ASIO, AUX/monitor, state recall and hotplug. No parallel CI matrix or redundant always-on worker.

## 2026-11-03 single production authority cleanup
Real-time callback now routes through the explicit `NativeRenderAuthority` decision. The no-longer-reachable legacy stereo FX processing code has been removed from the diagnostic-only path; this does not change accepted 10CH/4AUX production routing. `PreparedNativeMixerSession.h` remains tested but is no longer listed in the shipping GUI application target. See `docs/ENGINE_RUNTIME_AUTHORITY_20261103.md` for the decommission map and strict migration gates.
