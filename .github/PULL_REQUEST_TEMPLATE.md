## Product outcome
What user-visible behavior or problem does this change address?

## Reconciliation (brief, informational — NOT an owner/permission gate)
- Current `main` / related implementation inspected (SHA or link, if relevant):
- Existing accepted GUI/audio/session behavior to preserve:
- Overlap with other PRs, if any, and how resulting Git changes were reconciled:

**Anyone may work on any file on their own branch.** An open/draft PR, previous file owner, workstream label, missing external thread permission or same-file overlap never blocks starting a change. Resolve actual Git conflicts and verify behavior **at merge time**; see [Non-Blocking Parallel Development](../docs/architecture/PARALLEL_THREAD_CONVERGENCE_20261118.md) and [AGENTS.md](../AGENTS.md). The accepted modular MVVM direction is in [PR #763](https://github.com/masarray/arsonmix/pull/763), not a second competing ADR.

## Verification (report only applicable evidence)
- Current PR HEAD / relevant tests executed and result:
- Approved `/preview/` and native ACK/PCM/.smix regression, if affected:
- Installer compiled FX catalog and asset parity, if releasing:
- Physical ASIO/vendor test: PASS / FAIL / PENDING / NOT APPLICABLE:
- Known limitations and rollback:

No new coordination CI is required by this template. Do not confuse source PASS with physical FAT, a logical ACK with installed PCM, or merged code with a published installer. Preserve the real-time render authority and approved GUI.
