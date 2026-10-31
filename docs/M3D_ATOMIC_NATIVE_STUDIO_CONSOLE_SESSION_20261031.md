# M3D — native atomic Studio Console session, Bussing transaction + rollback

Date: 2026-10-31. Owner: dynamic Studio Console #192. Builds on merged M3C #246, existing M3A/M3B.

## Source delivered
- `DynamicAcceptedSession.h`: **one native control-thread authority** for accepted ChannelId roster and dynamic prepared PCM publisher. Prepare a copy, validate action, compile one complete graph, publish under existing device epoch/graph revision and all-output DISARM gate, and **only then** replace accepted roster. Invalid cycle, port/source mismatch, armed output, failed memory budget, stale revision, device epoch, and invalid edit cannot leak into accepted GUI model or live PCM. One native revision-scoped `DynamicConsoleRoutingView` accompanies each successfully prepared graph.
- `DynamicChannelRoster.h`: native `createStandaloneBus` (zero-source independent Group or FX Return with default primary OUT→MAIN, no CH10 borrowed) and `removeBusAndRestoreSources` (incoming primary outs rerouted to MAIN; sends addressed to explicitly deleted Bus removed; all edits are all-or-nothing). Existing `createBusForSelected` remains native selection transaction: Group **replaces primary OUT**, FX Return preserves primary OUT and adds parallel send.
- `DynamicAcceptedSession::setLiveMix/setLiveParallelSend` acknowledges native atomic ChannelId parameter control then stores the accepted fader/pan/send targets in control-thread roster **without topology revision increment**. A later Bus/Show compile cannot regress to stale gain; no graph compile, VST creation, heap, lock or callback stop for routine gain control.
- `tests/dynamic_accepted_session_test.cpp` covers actual PCM with two source mics, independent MAIN/MONITOR, nested groups, FX Return parallel, live gain preservation across multiple new graph revisions, empty Bus, Remove Bus reconnect, OUT NONE with SEND intact, stale device and graph revision, rejected armed-output edits, invalid main feedback, missing physical source rollback, muted outputs and unpublish fail-silent. `CMakeLists.txt` adds one dedicated native CTest to existing Windows test suite.

## Benchmark-inspired daily operator contract
1. SELECT MIC1 + MIC2 → Add Bus for Selected → **one** native transaction → new Vocal Bus ID (not borrowed input), both MIC primary OUT exclusively to Vocal Bus → BUS OUT MAIN; existing FX Sends unchanged.
2. Console Add Bus / Add FX Channel without selected input → distinct stereo Bus strip with zero sources and OUT MAIN, and no accidental sound.
3. Add FX from selected MIC → parallel POST send to FX Return; MIC primary OUT remains unchanged.
4. Remove Bus → native rewire primary inputs to MAIN, remove Sends addressed to the deleted Bus and dispose of Bus after safe graph publish. Confirmation/UI must disclose that sends to removed Bus will be detached.
5. Operator fader, pan, existing PRE/POST send gain → native atomic target with bounded 64-sample smoothing; stored for next graph compile and persistent Show serialization; a **failed or stale** edit never changes accepted routing.

## What remains blocked
This is native **control + prepared PCM foundation**, not integrated into the one production JUCE callback. `LiveAudioEngine` still owns current 10CH+4 AUX `LiveDryRoutingLab`, CH1 legacy VST/Orbit handoff, Show fixed schema, hardware OUTPUT1 and native physical output pair. Do not enable UI dynamic +CHANNEL/+BUS by misrepresenting offline coordinator as an active runtime.

**Next non-negotiable M3E/FAT:** unify ChannelId FX instance leases and vendor editor/Show identities with `LiveChannelFxGraph`; connect `DynamicAcceptedSession` in place of—not alongside—legacy dry renderer inside the single existing JUCE callback; establish 0FX old/new PCM A/B with physical output selection, SOURCE masks, MAIN/MONITOR/secondary output, device stop/reconnect and exact hardware ACK. Native snapshot -> keyed WebView Console, multi-select and Add Bus only after actual callback cutover. Hardware 0FX crackle issue #15 still release-blocking. Keep no virtual-cable/OBS assumption, no guaranteed literal zero-millisecond latency.

## CI discipline
One meaningful PR; add native CTest to already existing Windows Native workflow, do not create another workflow or matrix. Exact-head Windows MSVC/CTest and current Chromium regression must pass before merge. Physical FAT cannot be inferred from CI.
