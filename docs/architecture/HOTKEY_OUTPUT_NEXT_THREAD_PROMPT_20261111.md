# ArSonMix Hotkey + FL Studio External OUT — Next Execution Thread Prompt

**HISTORICAL combined H+O mission. H1/H2 are already MERGED (#536/#551). New work should use the [O-ONLY implementation prompt](MILESTONE_O_MULTIOUTPUT_NEW_THREAD_20261111.md); do not reimplement H.** The technical UX/native contract is [FEATURE_HOTKEY_FL_OUTPUT_EXECUTION_20261111.md](FEATURE_HOTKEY_FL_OUTPUT_EXECUTION_20261111.md). This file authorizes NO unqualified release or unsafe output reassignment.

Copy the following mission into the next thread (or follow it directly):

> GitHub Connector — https://github.com/masarray/arsonmix
>
> IMPLEMENT, do not rewrite the plan. First read latest AGENTS.md, docs/SMART_CI_RUNNER_WORKFLOW.md, docs/architecture/README.md, FEATURE_EXTENSION_PLAYBOOK.md, MAINTAINABILITY_EXECUTION_V1_20261111.md, and **FEATURE_HOTKEY_FL_OUTPUT_EXECUTION_20261111.md**. Fresh-check main HEAD, in-flight PRs, actual native GUI /preview/ build and exact code paths. Do not trust the 2026-11-11 baseline as immutable.
>
> Product scope, in this order:
>
> **H1 (FIRST, working vertical milestone):** Right-click native ArSonMix GUI controls -> Add Hotkey -> movable compact "Please press Hotkey" card -> Save/Cancel -> compact searchable register grid (shortcut, stable target, App/Session scope, state, edit/delete). Real Win32 RegisterHotKey/WM_HOTKEY (with MOD_NOREPEAT) must work while another app is foreground and ArSonMix minimized. Native typed action dispatch, stable ChannelId, session and global persistence, duplicate/conflict rollback, true native ACK/readback. Reuse web/approved-native-context-menu.js, preserve Rename/Delete/Orbit and exact /preview/. No JavaScript-only hotkey, no simulated mouse clicks. Ship C++/JS/Chromium tests with functionality.
>
> **H2:** For third-party VST3, audit actual JUCE VST3 host support for optional IComponentHandler3 and stable vendor ParamID; when verified use native parameter-context menu. Always provide supported FX-header Last Touched and searchable Parameters fallback for exposed automatable controls, unlike false promises of right-click on every vendor widget. Avoid unstable parameter index; missing plugin maps stay unavailable. Test actual vendor instance MOVE/restore and parameter identity.
>
> **O1:** Make FL Studio-familiar selected-channel ROUTE control: separate INTERNAL OUT vs one stereo EXTERNAL OUT selector for INPUT/GROUP/FX_RETURN; (None) + current device's real enabled ASIO stereo pairs. Choosing EXTERNAL OUT must NOT silently disconnect existing MAIN/BUS WIRE. Implement real BUS/channel post-FX/fader -> chosen physical port PCM as one bounded native output plan in existing DynamicNativeSessionPublisher/LiveDryRoutingLab/LiveAudioEngine callback, no second renderer/VST execution. Reuse existing OUTPUTS matrix for advanced verification and existing .smix/HardwareBinding with stable ChannelId/profile-pair persistence. Safe transition, device epoch/revision, stale-ACK rollback, mismatch Needs Remap, exact output name, shared-pair collision and duplicate graph-path diagnostics. No invented ports, extra Master, 2/3/4-way DLMS or independent-soundcard sync.
>
> **O2:** Exact-head native/GUI/installer and physically witnessed ASIO FAT: MAIN1/2, BUS A3/4, BUS B5/6, BUS C7/8 on an actual 8-out interface, distinct output capture, one FX render per instance, 44.1/48k, multiple verified buffers, PDC, mute/solo, SOURCE ONLY with external still audible, handling existing MONITOR pin conflicts, secondary WASAPI copy unaffected, 0-FX crackle and pop/click, reconnect and session restore. If physical rig absent, mark hardware FAT PENDING, never claim production-ready.
>
> Each milestone is one meaningful integrated code PR, not a planning PR or micro-PR chain. Start H1 actual implementation immediately, source audit inside that PR. Preserve one approved /preview/ and one JUCE callback. Do not merge another active branch by accident. Use focused quick tests, then owner-authorized exact-SHA Smart CI once per needed UI and native/FAT milestone. Do NOT ask human to trigger GitHub Actions manually. If installer needed use UI+FAT rather than UI+native+FAT. Publish real artifact/status, not simulated GUI. Report current SHA, changed files, tests, actual FAT status, blockers, next action. No "wait for next thread" instead of code.

## Minimal milestone completion check

| Stage | Real deliverable | Critical fail condition |
| --- | --- | --- |
| H1 | Native global shortcuts + correct target/actions + persistent compact UI | Background shortcut doesn't work, conflict overwrites existing mapping, unstable channel/FX ID, or native ACK missing |
| H2 | Vendor-capable context menu or safe host parameter fallback | Generic click on vendor window is assumed available, parameter list ordinal silently retargeted |
| O1 | Single external OUT pair per channel/BUS + actual native PCM + same internal WIRE | Only GUI text changed, MASTER silently disconnected, invalid device pin auto-selected, FX rerendered |
| O2 | 8-out physical proof, regression log, qualified Windows artifact | Claimed hardware success from tests only; crackles/unsafe monitor output unresolved |

Keep this prompt short; all implementation truth tables, source benchmarks, UX failure modes, data schema and acceptance IDs are in the linked full contract.