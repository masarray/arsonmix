# ZBSH-10 — integrated adversarial regression

Integration candidate from main 7160e6bdef67240001726fa1ba888787f665c6cd with exact ZBSH-04–09 heads (#938–941, #943, #944). Only Main.cpp and CMakeLists.txt overlap; scanner-worker launch/teardown and runtime journal/CTest changes explicitly reconciled. Other blobs copied from exact reviewed heads. Prior branches retained until integration qualifies. No release.

## Actual regression scope
- Strict UTF-8 decoding of persisted text/receipt before JUCE parsing (NUL, overlong, surrogate, truncated and out-of-range code points); bounded read and structural nesting; transactional caller outputs.
- Scanner receipt XML depth preflight before recursive parser; no DTD, nested plugin nodes, wrong nonce or path. Discovery isolation is not a privilege sandbox.
- Managed Show generation pointer reads/writes; workspace Windows device/alias/control-character path rejection; release ZIP Windows aliases and file-as-parent collisions, duplicate metadata keys, setup ZIP declared size/type.
- Dedicated read-only pinned-JUCE Linux job compiles actual storage code under ASan/UBSan, runs six native fixture executables and 20,000 libFuzzer inputs with fixed seed against JUCE JSON and Show/Snapshot codecs. No user files, network vendor DLL or app execution in fuzz target. Existing sanitizer, PowerShell, scanner and Windows native tests remain. Exact run results are evidence, not merely workflow creation.
- Required merge status adds securityRegression lane for explicitly matching paths; no stale head, wrong job, failure or missing run accepted. Effective only after trusted main deploys new code. For this integration itself verify dedicated checks manually as well as existing required gate. No protected status spoofing or bypass.

## Residual scope
No proof of total security; no full OS sandbox, commercial signing, full transitive SBOM, arbitrary runtime native fault isolation, full native JSON duplicate-key grammar, WebView origin review, hardware FAT, or live publisher/updater attestation test. Fuzz target covers bounded text and Show codec, not all native session/plugin/ZIP grammar. Confirm actual Windows full CTest, full Chromium and previous security jobs on combined head before merge. User-side GitHub ruleset/account controls and first attested installer migration remain separate. No change to audio DSP buffering/latency.
