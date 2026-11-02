# ArSonMix ENGINE — One Owner Reconciliation / Next Work Handoff
Date: 2026-11-03. **ENGINE workstream only**, binding on the #266 implementation branch.

## Reconciled branch ownership (avoid two parallel engines)

| Scope | Single owner | Allowed work |
|---|---|---|
| Native ENGINE | **PR #266** `milestone/m5-universal-channelid-vst3-lease-migration-20261101` | ONE `LiveAudioEngine` callback, full-fidelity M4→ChannelId retirement, VST3 instance/editor/Show ownership, native FX/CPU/memory, device/epoch |
| M1 stabilization | Former **PR #287**, absorbed as FAST-FORWARD ancestors of #266; close #287 after verification | No independent engine feature branch. The 64-sample fader, AUX parity, bounded graph retirement, Show worker lifetime, removal of dead diagnostic FX must stay in #266 |
| GUI production adapter | PR #271 | WebView renderer consumes **accepted** snapshot/ACK, never owns PCM |
| GUI reference | PR #278 + #285 | Visual and WIRE workflow only; no browser FX or duplicate JS graph may be merged into DSP |
| Final integration and physical FAT | PR #270 / issue #192; physical crackle issue #15 | One joined Windows exact-SHA artifact, real hardware tests. Not an ENGINE branch |

**Important:** At reconciliation, comparison of `main@1859fa147b360e607b29a8eba636af33288a3a36` with ENGINE #287 `1e21bf0a0d8aff33b02b56cec5c2e5c328fd41d1` was **diverged** (78 commits missing from ENGINE's source history, 62 ENGINE-side commits). This is a warning, not a merge resolution. Refresh main and reconcile line-by-line against existing native behavior before production merge. Do not overwrite 78 upstream changes with a branch checkout or careless `git reset`.

## Single native render truth NOW

```text
Approved GUI workflow (#278/#285) → native GUI (#271)
  → ConsoleLiveBridgeV6: schemaVersion=6, canonical decimal ChannelIds,
    unique requestId, accepted snapshot/ACK and truthful capability flags
  → Native JUCE LiveAudioEngine (ONE production callback)
     ├─ NORMAL, unpatched → SILENT (no hidden CH1)
     ├─ accepted M4 → LiveDryRoutingLab + original CH1 vendor/Orbit lease
     │                + 10 input + 4 AUX + MAIN/MONITOR (real PCM)
     └─ diagnostic mode (no M4 owner) → isolated 0FX test tone/capture
  × DynamicChannelRoster/FX/Publisher: prepared/offline only, NOT currently live
```

Do not confuse multiple **types** in the tree with multiple **active** engines: their runtime entry point and ownership decide whether they are a problem. `PreparedNativeMixerSession` is testing-only; `NativeShowSaveWorker` is I/O, not a render worker. Legacy M4 render path is a deliberately **quarantined compatibility implementation**, not an approved target for new GUI features.

## CLEAN / FROZEN / RETIRE inventory

- **CLEAN NOW**: old diagnostic stereo VST graph removed; one explicit `NativeRenderAuthority` selector; separate test-only source registration; M1 live AUX/fader, bounded retirement, worker resource release tests.
- **FROZEN**: M4 fixed ordinal 10CH+4AUX may only receive production bug/safety fixes (driver, crackle, VST/Show regressions). Do not implement CH11, Group, new FX Return or WIRE semantics as another M4 feature.
- **PRESERVE**: CH1 original prepared VST3/Orbit/editor/Show, other strip/aux FX, independent MONITOR, AUX PRE/POST, sparse device port maps, direct route fence, mute and diagnostic capture evidence. Do not delete before parity.
- **UNADMITTED**: dynamic ChannelId graph, `UnifiedLegacyProductionAdmission`, `DynamicNativeSessionPublisher` are one planned replacement; not an alternate live callback, and `productionCallbackLive=false`, `supportedOps=[]` are intentional.
- **HISTORICAL**: separate draft experiments (including #251 WASAPI and #225 FactoryFX) do not acquire production engine ownership by existence. Do not close unrelated issues without evidence; do not blindly merge.

## Non-negotiable invariants (automatically guarded)

The cheap `tests/engine_runtime_reconciliation_test.mjs` runs **before** JUCE checkout in the existing Windows job. It fails on extra production callback, double M4 PCM render, test-only mixer listed as native app source, diagnostic VST resurrection, lost CH1 vendor lease, false ChannelId LIVE capabilities, or the loss of Show worker isolation. These are **migration stage gates**: a deliberate universal cutover must update them in the same exact-SHA commit that establishes real callback/VST/GUI/FAT proof; never bypass by changing a string only.

## Next ENGINE milestone (one substantial outcome)

1. Rebase/reconcile #266 on current `main` **without** overwriting GUI/native/diagnostic changes. Detect conflicts particularly in `src/LiveAudioEngine.h`, `src/Main.cpp`, `src/LiveDryRoutingLab.h`, `engine/core/PreparedMixerBlock.h`, `CMakeLists.txt` and mixed native/JS source contracts.
2. Migrate the **actual** M4 owners to one accepted ChannelId graph: CH1 original vendor/Orbit editor, CH2–10, AUX1–4, MAIN/MONITOR, Show states and sparse real hardware pins; no reinstantiate on routine fader/mute/send edits. Prepare on control/worker, not callback.
3. Switch the existing JUCE callback **once** with a full-fidelity native epoch/graph ACK; maintain one output pathway, no phantom channel and no double FX. Device/clock/FX owner changes fail closed until proven safe.
4. Update `ConsoleLiveBridgeV6` supportedOps/capabilities only from **real admitted callback**; integrate #271 operator intent and #278/#285 approved WIRE through the **same graph**; #270 owns combined exact SHA release.
5. Remove M4 compatibility adapter and ordinal CH1 paths **in the same** production integration only after golden 10CH/AUX/monitor PCM and real vendor/editor/Show parity, approved Windows native CI and ASIO 0FX/1FX hardware FAT.

### Definition of ready for hardware/UAT

A green PR-specific CTest is valuable but **insufficient**. Require exact joined Windows EXE provenance, hardware driver/rate/buffer/pins, 0FX clean wired audio, real VST3 editor/state, MUTE/SOLO/AUX/MONITOR, Show recall, and measured output latency/crackle evidence. Do not claim zero physical latency, zero crash risk from arbitrary in-process third-party VST3, or universal CH11 before hardware validation.

**Do not start another ENGINE PR while #266 is open.** Future ENGINE edits and test evidence go into #266; #271 (GUI) and #270 (FAT) receive only explicit contract handoffs.

## M-U2 canonical realtime control kernel — 2026-11-03
The prepared universal `DynamicPreparedMixerBlock` now uses **one reusable** `RealtimeSlew64` for ChannelId fader/PAN stereo targets and all PRE/POST SEND targets. CONTROL writes one atomic/latest target per logical control; AUDIO reads once per channel/send per block and advances exactly 64 samples across variable 4/16/32/12-frame ASIO/WASAPI quanta. A reversal starts from the last *applied* coefficient, not the most recent requested goal. No extra worker, event FIFO, heap, lock, division, or plugin re-instantiation in RT. The single native `M3DAtomicStudioBusFxChannelSessionRollback` CTest now checks actual ChannelId→wet PRE→MAIN and POST→MONITOR PCM with short callbacks, coalescing, revision rejection and null output at the terminal zero gain. The installed JUCE callback remains safely on accepted M4 while full vendor/Show migration remains pending; no fake GUI LIVE promotion. This removes a genuine small-buffer dezippering blocker for the final engine, not an extra audio engine.

### M-U2 efficient GUI control ownership
`DynamicPreparedMixerBlock` reuses its compiler-built `ChannelId → dense lane` table after graph admission, so fader/PAN and PRE/POST SEND control commands do **not** linearly scan all channels per gesture. The table and CSR send adjacency are accessed on the control writer only; realtime callback reads packed atomic targets once per block, advances sample-continuous state, and does not hash, allocate, lock or invoke workers. Regression sends **4096 successive fader gestures**, tests latest-target-wins, unknown ChannelIds fail closed and exact PCM after 64 rendered samples. A lock-free atomic requirement is enforced at compile time for send target storage. The existing Windows job builds these tests via the established M3D test dependency; no new workflow or CI matrix.
