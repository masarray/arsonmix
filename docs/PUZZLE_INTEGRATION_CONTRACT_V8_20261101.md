# ArSonMix V8 — Puzzle Contract / Three Independent Modules, One Product
Date 2026-11-01. Normative product PRD/ARD v5 and real-time safety v6 remain binding. This file **replaces v7 work sequencing**, not product scope. No customer testing until complete integrated mixer.

## Root-cause correction
Our old "ENGINE then GUI then FAT" planning was a dependency waterfall disguised as parallelism. Each owner worked against different guesses of the native protocol. New execution uses **contract-first parallel implementation**: freeze one executable exchange shape and use an integration harness as the continuously maintained socket between modules. A module is deliverable only if it plugs into the socket on the latest integration baseline. Separate CI GREEN is not acceptance.

## The three puzzle pieces and their SOCKETS
**Piece A — ENGINE runtime** (#266; sole owner src/**, engine/core/**, C++ runtime tests). Provides `IConsoleRuntimeV8` through the existing JUCE WebView events, NOT a second engine. Input: versioned ConsoleIntent. Output: accepted ConsoleSnapshot, ConsoleAck and measured PCM/ports. One primary realtime callback; M4 CH1 VST/Orbit+all channels/AUX/Show migration preserved; true dynamic ChannelId/group/wet return/monitor and plugin ownership. No fabricated capabilities. **Until live callback is installed, all mutating ops are rejected and no one claims this piece connected.**

**Piece B — GUI adapter and display** (#271; sole owner web/** and UI tests/generated embedded HTML). Consumes EXACT JSON fixtures emitted by Piece A; sends only approved intents; displays only native accepted state. Does not create channels, audio engine, fake ports or guessed aliases. Can develop UX against frozen fixture in parallel, but its own test must replay native producer fixtures and assert source→pending→ACK→snapshot, rejection and silent output. All common IN/OUT/FX/SEND actions accessible without massive modal.

**Piece C — INTEGRATION harness/release** (#270; sole owner cross-layer fixture/contract tests, CI YAML, package/FAT). Maintains ONE contract validator and ONE deterministic loopback harness that runs Piece A producer against Piece B consumer on every integration promotion. NOT a separate codepath for another GUI/engine. Test vectors must be produced by actual C++ adapter or checked-in native-golden emission with traceable generator, **never hand-authored alternate shapes**. C merges owners in a controlled branch only after both satisfy the same exact fixture version, then verifies source SHA/EXE, PCM, UI, device+hardware evidence.

## SINGLE WIRE CONTRACT — signed-off fields, no parallel interpretations
Events: `arMixConsoleSubscribe`, `arMixConsoleSnapshot`, `arMixConsoleIntent`, `arMixConsoleAck`. JSON schemaVersion = 6 during v8 *workflow*. Channel IDs, deviceEpoch and graph/roster revisions are canonical base-10 uint64 STRINGS. RequestId is opaque printable string prefixed with a **unique WebView session nonce**. Physical source port is distinct from ChannelId and display strip index.
- Intent: `{requestId,deviceEpoch,baseGraphRevision,baseRosterRevision,operation,targetChannelId,payload}`.
- ACK: `{requestId,status,deviceEpoch,graphRevision,rosterRevision,errorCode}`; status ACCEPTED / REJECTED / REQUIRES_DEVICE_CHANGE, same requestId.
- Snapshot: `{schemaVersion,productionCallbackLive,device:{epoch,backend,captureDevice,playbackDevice,inputEndpoints,outputEndpoints,enabledInputs,enabledOutputs,sampleRate,bufferFrames,outputArmed},graph:{revision,rosterRevision,channels,destinations,capabilities:{callbackOwnsChannelIds,supportedOps,dynamicChannels,groups,fxReturns,monitor}},telemetry:{rawCapturePresent,rawCapturePeak,mainPcmPresent,monitorPcmPresent}}`. Every optional extension documented; NO required field may be silently absent. Actual device backend pins, audio measurements and installed capabilities only come from A.
- All three modules must import or validate this SAME wire; B and C may never redefine incompatible schema. Contract changes require one issue #192 proposal with old/new golden JSON and updated tests from EACH module **in a single integration checkpoint**, not unilateral changes.

## PARALLEL SPRINTS (not waterfall)
**S0 — SOCKET LOCK, bounded:** Piece A emits native v6 `SNAPSHOT_UNREADY`, `ACK_REJECTED` golden fixtures reflecting real producer (including enabled pins); Piece B demonstrates renderer for SAME fixtures without fake LIVE; Piece C passes the actual A→B trace and rejects missing required fields/duplicate ID. ZERO product vanity work. Exit: one integration test with zero adapter field mismatch. If no real producer fixture, STOP all feature expansion and fix contract.

**S1 — REAL FIRST SOUND VERTICAL SLICE in parallel:** A installs one callback/channel chain with capture IN1→arbitrary CH07→MAIN, real fader+mute, output and measured raw PCM; preserves M4 external VST and other lanes. B exposes direct CH7 IN+MAIN OUT and compact device picker using S0 socket. C drives the actual A command into B, verifies accepted snapshot and PCM, and asserts 1-pin cannot produce pseudo stereo IN1/IN1. Exit: ONE full connected live path, internal automated test. Never ask user to test.

**S2 — FULL CONSOLE VERTICAL SLICE in parallel:** A completes 10CH/4AUX/2 genuine independent VST3/Show and dynamic CH11, serial Group, parallel wet FxReturn, Monitor pins/FX editor and safe ops; B adds actual per-role FX/SEND/OUT/groups/return/monitor/Show UI only as each native op appears; C expands ONE scenario to IN1→CH07 + IN2→CH02→VST3→GROUP→MAIN, parallel SEND→FX_RETURN plus MONITOR, native accepted ACK, rollback and real PCM. Exit: one integrated Windows app feature-complete, no offline surrogate.

**S3 — QUALIFICATION:** C owns one Windows+Chromium combined exact-SHA gate and executable provenance. A fixes RT/driver/crackle, B fixes GPU/layout usability. Hardware QA independent if available; status pending otherwise, no dishonest certification. Only after completed S3 do we invite customer UAT.

## DAILY HANDOFF PROTOCOL
Every owner posts to epic #192 a **Puzzle Handoff** (one message, not 20 micro statuses):
1. `Piece=A/B/C`, commit SHA and changed files, exact integration baseline SHA.
2. `Socket=` schema version, fixture hash, test command/result.
3. `Connected capability=` a real production operation proven end-to-end (or explicitly NONE).
4. `Blocked by=` one field/operation mismatch, exact owner, failing test and one corrective action.
5. `Next vertical slice=` code+test artifact, not "improve visual".

Integration owner is the **traffic controller**: no new feature work if S0 schema is divergent, no native callback not bound, no GUI green called installed mixer, no FAT synthetic called hardware. When a block is found, C files one precise issue for its owner and tests an integration candidate; A and B fix their own code immediately. Do not proliferate branches (#274/#275 stay patch sources, #276 staging only). Merges only after same-fixture checks and same-tree tests. Code owners are responsible for running cheap source/generator checks *before* pushing; cancel superseded CI runs.

## STOP-SHIP CONDITIONS
No active native listener dispatched to actual ChannelId graph; GUI in demo mode or hidden behind legacy M4; schema missing required native output pin fields; standalone HTML generator parity red; VST insert fake or CH10 label pretending FX Return; source ACK with no audio evidence; Windows/Chromium different SHAs; physical crackle untested being called clean. If any occurs, say **NOT INTEGRATED**; report corrective code required, not percentage progress.
