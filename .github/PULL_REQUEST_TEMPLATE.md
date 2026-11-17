## Product outcome and source of truth

User-visible acceptance:  
Linked feature / architecture issue:  
Current `main` SHA verified immediately before writing:  
Open overlapping PRs and **exact HEAD SHAs** inspected:  
Existing accepted visual/audio/session checkpoint protected:  

**This is one coherent product integration PR**, or explicitly a documentation/research-only PR. It is NOT a legacy GUI/ENGINE/INTEGRATOR/FAT four-lane handoff. The old lane template was superseded by [AGENTS.md](../AGENTS.md) on 2026-11-05. Follow [current scalable architecture ADR](../docs/architecture/ADR_MODULAR_MVVM_REALTIME_20261118.md) and [parallel thread convergence](../docs/architecture/PARALLEL_THREAD_CONVERGENCE_20261118.md).

## Domain ownership / parallel reconciliation

- Integration owner (this PR / receiving PR):
- Subsystems and exact changed production files:
- Existing implementation reused (issue/PR/commit):
- Shared contract or native ABI changed (ChannelId/EdgeId, epoch, FX, bridge, `.smix`, device, or **none**):
- Concurrent same-domain/same-file conflicts identified:
- How unique changes were reconciled without overwriting newer accepted source:
- Changes explicitly excluded from this PR:

For an established single-product integration PR that uses the historical workflow compatibility marker, include it **in the PR body intentionally**, not automatically for every draft: `<!-- arsonmix-integration: single-owner -->`. This marker **does not** authorize bypass of required CI, protected-main, actual source reconciliation or physical output safety. Research/docs-only PRs do not need a product marker.

## Complete, exact-head acceptance evidence

- Recorded PR HEAD SHA / merge base:
- Targeted fast contracts, real command/results:
- Native JUCE/PCM/VST3 tests (exact run URL or **NOT RUN**):
- Approved `/preview/`/Chromium tests (exact URL or **NOT RUN**):
- Installer compiled-product/ImmerSIB/SonKu/asset parity (exact artifact SHA or **NOT APPLICABLE/PENDING**):
- Physical ASIO/WASAPI/real vendor FAT (**PENDING** unless witnessed):
- Rollback route and protected user/session data:
- Remaining blocker, dependent PR and next **meaningful** product milestone:

**Never claim** an offline logical ACK as installed PCM, CI PASS as physical FAT, merged code as a published installer, or a stale historical PR as current main. No new alternate GUI, callback, session authority, or duplicate DSP graph. Do not create extra paid Windows runs for documentation; FAT already covers native compilation when a field installer is required.
