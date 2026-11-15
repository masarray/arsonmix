# ImerSip approved audio checkpoint — editor lifecycle + host power
Baseline: 30f3e333368cc3ca97b568c91f05a63f07e80980 on immutable branch checkpoint/imersip-180deg-approved-audio-20261115.
Owner likes current 180° sound. Do not change HRIR, music MID/SIDE, P8.7, concert room, bass, audio callback, preset, or .smix state.
Fix: titlebar X hides editor; exact ChannelId/lane + slot + instanceId + epoch + revision re-show retains SAME JUCE window and its graph lease, with active-owner check. New target uses original guarded lease+new editor; no new audio authority.
Host native Power uses exact rack bypass per-insert state and fade (not new DSP flag) via bound Main.cpp control callback. Disabled when unbound, per-slot atomic state readback, live refresh. Closing window clears transient Original A/B.
Layout 980x560: enlarged top-view, readable aligned 4x2 knob grid, labeled spatial/tone sections, separated venue/actions/power/status. No forced bold cards.
Gates: native JUCE compiled editor/processor test checks power authority binding, clean viewport, same-processor open-close state, PCM accepted settings; Windows JUCE app and portable on same commit required. Manual Windows rack click -> X -> reopen -> repeated reopen -> power OFF/ON while meters continue -> save/open verification still required before owner acceptance.

## Final native slot-power semantics and lifecycle regression
- INSERT ON / INSERT OFF is the bypass state of **this exact insert**, not the channel-wide rack master gate. Original A/B is temporary audition, not power. The editor caption uses this unambiguous vocabulary; all power changes still call Main.cpp native token/lease-guarded bypass callbacks, not a second local DSP authority.
- JUCE native test now exercises visibility false→true on the same editor pointer with bypass retained, with a distinct master-rack state not altered by insert toggling. A fresh editor is intentionally power-disabled until the host binds the exact verified rack slot. This is a control/UI-only regression: source-exact approved 180° DSP and .smix format untouched.
