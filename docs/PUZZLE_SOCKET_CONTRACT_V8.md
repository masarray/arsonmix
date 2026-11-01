# Three puzzle sockets — executable shared contract, not another roadmap

**Canonical source:** integration/console-v6/fixtures.json + integration/console-v6/puzzle-seam.mjs
**Test:** node --test tests/console_v6_puzzle_seam_test.mjs
**Promotion-only blocker:** node scripts/check_console_puzzle.mjs --release

## Three handoff artifacts required for one product
ENGINE #266 supplies **actual versioned native \`arMixConsoleSnapshot\`**, the only authoritative hardware masks, 64-bit ChannelId roster, VST ownership/capabilities, PCM, plus an \`arMixConsoleIntent\` → \`arMixConsoleAck\` handler; it must implement the real callback. ENGINE must export a normalized *producer-generated* JSON snapshot capture to be replayed by GUI+FAT tests; the present fixtures are intentionally synthetic.
GUI #271 MUST use exactly the shared fixture and the native emitter's production snapshot, not \`MAIN\`/\`BUS-FX\` as invalid ChannelIds in fixtures. Operator actions only via native intent/ACK, not UI-local accepted graph.
FAT #270 MUST load the same fixture and then the ENGINE **actual producer capture**. It owns both existing CI workflow YAML, one combined SHA integration branch and release gating. Synthetic PCM vectors prove only numeric validator behavior, not real callback audio.

The static \`--release\` source gate intentionally **fails until** all three owners implement and connect those sockets. This is a *minimum* gate and does NOT certify actual audio, plugin DLL execution, no crackle or sound. Real CTest/Playwright and physical FAT (#15) remain separate hard gates. Never change checks to success by weakening functionality. If a migration changes the genuine schema, update this single contract in a reviewed commit and regenerate both consumer tests together, rather than implementing three private schema forks.

## One ownership and integration pipeline
- ENGINE owns src/** engine/core/** and native C++; GUI owns web/** UI tests and generated HTML; FAT owns scripts/console_v6_fat_gate.mjs tests/console_v6_fat_gate_test.mjs and .github/workflows/**. This directory and its fixtures are an **interface lock** edited only by integration/orchestrator and explicitly reviewed by all owners.
- G0: each owner tests the shared contract locally; GUI/FAT first replace private fixtures by loading this one fixture. ENGINE produces actual runtime JSON readback with exact field names and stable decimal IDs. No user testing.
- G1: ENGINE publishes full M4-fidelity callback and real native supportedOps, GUI consumes only that, FAT tests actual native PCM. A native \`REJECTED\` is not "feature implemented."
- G2: CH11, group serial route, wet FX Return parallel send, independent Monitor, independent VST leases/Show, native readbacks all audible. Use one integrated branch and same SHA.
- G3: run \`node scripts/check_console_puzzle.mjs --release\` on combined SHA, then one Windows MSVC/CTest+package and one Chromium/Playwright, plus independently witnessed physical hardware if rig available. Do not claim acoustic clean from browser/mock/synthetic tests.
- G4: only one feature-complete product candidate offered to user for UAT after internally successful gates.

### Immediate P0 cross-thread bugs proved by audit
- ENGINE current bridge emits \`graph.channels=[]\`, \`productionCallbackLive=false\`, \`supportedOps=[]\` and unconditional native rejection; v6 GUI cannot operate. Stop reporting "real Console LIVE" before actual production callback.
- GUI mock tests have \`MAIN\`, \`BUS-FX\` nonnumeric IDs, contradicting native uint64 ChannelId.
- FAT synthetic snapshot requires \`enabledInputs\`/\`enabledOutputs\` absent in ENGINE native emitted snapshots. Green FAT on fabricated fields means nothing about integrated actual native.
- GUI CI repeatedly fails at generated standalone parity; regenerate after consolidated source changes before any CI push. ENGINE MSVC latest failed C2666 in C++ test juce::var vs int comparisons.
