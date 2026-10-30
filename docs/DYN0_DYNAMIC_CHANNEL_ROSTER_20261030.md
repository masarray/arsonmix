# DYN0 — Dynamic Channel Roster Foundation (2026-10-30)

Parent: [#192](https://github.com/masarray/arsonmix/issues/192), #170; code baseline main@928dfdf (G5B merged).
**Truth:** this is a control-thread state/ID/transaction foundation, NOT a live 128-channel audio engine, not a user-visible Add Channel control and not a second renderer.

## Architecture decision

The product must use a **single ChannelStrip type** for channel 1 through channel N. A logical channel ID is a stable uint64 identity independent of UI order or physical capture pin. Main, FX AUX, Group, Monitor and Input are roles on the same shared definition, not separate plugin or GUI implementations. Every channel has the same capability for FX inserts, gain, format, sends, mute/solo and automation; role-specific constraints apply only to legal routing destinations. A channel always owns independent vendor processor objects when LIVE; duplicating a descriptor may copy catalog IDs, but NEVER copy/share vendor instances.

The user chooses channel count through actual Add/Remove/Duplicate, without C++ code changes, after DYN1/DYN2 integration. Logical channel count and active ASIO/WASAPI physical port count are **independent**. There is no hard-coded 10/12 maximum in the new roster; memory budget is configurable and real-time DSP/CPU admission must check active graph size, deadline, plugin count, latency and available RAM. It is impossible to promise mathematically unbounded concurrent channel processing.

## DYN0 code

- \`engine/core/DynamicChannelRoster.h\`: control-only model; stable non-reused IDs, add/duplicate/remove/reorder, rename, mono/stereo, fader/PAN, lazy FX catalog references, send destination ID references, resource-based definition memory admission, revision fence, batch all-or-nothing rollback, immutable prior draft on error.
- \`tests/mixer_routing_plan_test.cpp\`: add 128/1024 logical descriptors, same FX API for CH128, reorder without identity changes, clone without sharing VST object, referenced-send deletion reject, invalid pan and stale revision rollback, memory-budget rejection. Run through the **existing** native CTest target instead of creating another heavy CI job.
- No changes to the current callback, CH1 / G5B plugin lifecycle, L2 UI, OS device, accepted route schema or currently shipping 10CH DSP. That limitation is intentional to preserve #189 MS2A, #190 G5C, #188 FactoryFX while replacing the fixed topology in the next integrated milestone.

## DYN1 — real audio acceptance (NOT YET DONE)

Replace the fixed \`stripCount=10\` compile-time arrays with a session-sized **single** prepared graph, explicitly bridging old 10CH Show V4 and preserving existing send/solo/FX/PDC invariants. Replace the \`processLegacyChannelOne\` CH1 branch with the same per-channel VST3 runner used for all other channels, with stateful migration of Orbit and vendor editor/insert IDs. Native tests must process real PCM for CH1, CH11, CH32, CH64, CH128 with correct FX, send, mute/solo, and no RT heap/lock/processor construction. Refuse resource-exhausted edits and leave the previously published graph intact.

## DYN2 — operator GUI acceptance (NOT YET DONE)

Only after DYN1 can \`+ CHANNEL\` create a real **native ACKed** audible input or bus receiver. Use one dynamic JS channel array keyed by ID, not ten literal indices; virtualize/bank the visible L2 strip components and meter updates (never render hundreds of active fader DOMs per frame). Stage structural ADD/REMOVE/DUPLICATE while MUTE; positive \`deviceEpoch/revision\` ACK changes LIVE, rejection rolls back and never unmutes. Show migration must preserve stable references and VST states.

## Guardrails

Do not claim Studio One/Fender Studio Pro or Waves LV1 parity from a roster. #174 typed buses and #175 hardware outputs are still necessary. Full portable Show, VST PDC, master FX and #15 zero-VST crackle hardware FAT remain release blockers. Do not silently add a second renderer. Existing [AGENTS](../AGENTS.md), R5, L2 #176, G5B #186 remain authoritative on delivered functions.

## External engineering references

- JUCE AudioProcessorGraph addNode/removeNode/update/rebuild: https://docs.juce.com/master/classjuce_1_1AudioProcessorGraph.html
- Steinberg VST3 callback threading and off-RT lifecycle: https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/API%2BDocumentation/Index.html
- Waves LV1 mixer configuration and channel layers: https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html
