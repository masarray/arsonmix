# ZBSH-07 — Native memory safety and repeatable instrumented tests

Base 7160e6bdef67240001726fa1ba888787f665c6cd. Scope is targeted first-party native boundary and lifetime hardening, not a claim of complete memory safety or formal verification.

## Concrete source changes
- Packed stereo control: guard non-finite/negative gain before float-to-unsigned conversion; saturate finite gain at 4. Preserve all valid coefficient bits, MUTE/SOLO, and current control validation. No sample-loop code added.
- Clock bridge sizing: reject unsupported rate/capacity bounds, promote FIR guard multiplication before arithmetic. Prevent signed overflow and unsafe float-to-integer conversion on hostile control arguments. Normal sizing remains unchanged.
- Adaptive clock bridge: stage ring/coefficient allocations before committing dimensions; catch allocation failure and preserve prior stopped configuration. Caller must still stop both callbacks before prepare/reset. No additional buffering or processing in push/pull.
- SPSC: enforce power-of-two capacity and bounded lock-free counters, use bitmask slot mapping valid across uint32 wrap. All existing instantiations audited use powers of two. No new lock or atomic operation.
- Existing graph retirement lease/handoff implementations remain unchanged; instrument actual concurrent publication and existing structural retirement tests rather than replacing a working architecture.

## Qualification
Linux standalone ASan+UBSan+float-cast-overflow (6 executables), separate TSan (3 executables). No JUCE or paid runner, no release compiler instrumentation, no suppression or continue-on-error. Includes new invalid-input, queue stress, and buffer bounds fixture plus existing graph/clock/PCM tests. TSan runner initialization failure is not a PASS; diagnose environmental failure separately. Exact-SHA Windows native qualification remains necessary through normal protected gate; sanitizer jobs are extra PR evidence, not newly required GitHub ruleset contexts. No real vendor/hardware FAT claim.

Monthly/manual CodeQL C/C++ no-build workflow on trusted main, least privilege upload to GitHub Security tab. CodeQL advanced setup may conflict with existing default setup; admin configuration not visible to connector. If blocked, maintainer chooses one configuration; do not disable protections blindly. No-build mode may miss configuration-dependent code, and third-party binary analysis is out of scope. Do not post sensitive alert details publicly. Treat any scan finding according to SECURITY.md. No claim of completed CodeQL scan before evidence.

## Remaining
Session/preset/update input fuzzing and limits: ZBSH-08/10; explicit VST runtime fault recovery: ZBSH-09. Raw driver pointer validity and array lengths are trusted callback ABI; instrumented tests cannot prove arbitrary vendor buffers valid. Full JUCE/ASIO/Windows instrumentation, adversarial parser coverage and physical FAT remain pending. No new audio FIFO, no paid tooling, no changed DSP algorithms.
