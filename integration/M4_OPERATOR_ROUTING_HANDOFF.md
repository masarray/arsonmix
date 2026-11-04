# M4 integration: operator routing admission, native DSP handoff

**One product:** existing approved \`/preview/\`, existing M2 native v8 protocol, ONE JUCE callback. Do not create GUI/PCM/graph alternatives.

## What is executable now (integration control plane)

\`integration/m4-operator-routing.mjs\` consumes the **already normalized/accepted** \`web/approved-native-contract.js\` snapshot, never Pages mock data. \`planM4OperatorIntent(accepted, gesture)\` can emit the existing native \`APPLY_WIRE_TRANSACTION\` for:
- \`ROUTE_ONLY\`: replace the source's exclusive primary OUT with a Group or MAIN, after cycle and parallel dry-path checks;
- \`SEND_POST\`: add a unity-gain POST send, including a wet-only-verified FX return, after duplicate and double-dry checks.

These are *candidate intents*, not applied changes. GUI/controller is still responsible for the same device epoch, graph revision, roster revision, request ID, native ACK and **later independently accepted readback**. In particular, do not paint a cable after the intent returns \`ok:true\`. No new source of truth is introduced.

This is not yet wired to the approved visual renderer: GUI owner must add minimal one/two-click gestures **only after** the owner accepts the intent API; retain all approved pixels and controls. Current M3 #402 remains the authoritative native adapter.

## Explicit fail-closed boundaries

- \`SEND_PRE\`: ENGINE #266's v8 WIRE decoder currently permits only \`POST_FADER\`, although older native DSP accepts PRE. Requires decoder + exact tap readback proof.
- \`SET_SEND_GAIN\`: existing \`readbackProvesIntent\` checks WIRE presence, not the exact accepted gain. Requires native gain authority and ACK/readback proof.
- \`GROUP_SELECTED\` and \`NEW_FX_RETURN\`: engine core has graph support, but public M2 controller must add exact structural accepted-readback tests before these are operator actions.
- \`SIDECHAIN\`: must use a separate detector-only VST3 AUX input bus, never an audible WIRE connection. \`src/VstBusLayout.h\` currently disables all auxiliary input buses; there is no verified detector-only edge or native readback. No sidechain intent may be emitted before the ENGINE owner supplies real bus layout negotiation, detector buffer processing, precise latency handling and accepted ACK/readback.
- \`PDC\`: unknown latency fails closed; nonzero latency requires native graph compensation. Low-latency live-monitor mode must not silently acquire high buffered latency.

## ENGINE owner (#266) handoff

1. One prepared graph with explicit primary OUT, separate parallel audible send and *detector-only* sidechain edge; no feedback across either dependency graph.
2. VST3 main stereo + optional auxiliary sidechain bus negotiation; no allocations/locks/plugin discovery within audio callback; genuine plugin/input bus ownership, enable/disable and readback.
3. Graph-aware alignment across mixed dry/wet, send taps and detector path, plus explicit tradeoff of LIVE_LOW_LATENCY vs STUDIO_COMPENSATED mode.
4. Native capability flags and exact accepted snapshot effects: PRE/POST, gain/tap, bus creation, plugin sidechain assignment, PDC and revisioned ACK. Until readback proves a new op, do not advertise it in \`supportedOps\`.

## GUI owner handoff

Reuse \`/preview/\`. Map operator shortcuts (Group Selected, New FX Send, Route Only, External Sidechain selector) to accepted native intents *after* their capabilities are installed. No changed layout, fake meter, alternate GUI or JS PCM.

## Acceptance and release

The M4 pure routing contract tests run in existing cheap \`scripts/check_console_puzzle.mjs\` before heavy Windows CI. They are source/control correctness only, not hardware FAT or real sidechain DSP. M3 #402/#266, Windows exact-head CTest, screenshot parity and physical #15 remain separate blockers. Never mark M4 complete or product LIVE based solely on these tests.
