## Product outcome
What user-visible behavior or problem does this change address?

## Reconciliation (brief, informational — NOT an owner/permission gate)
- Read [current INT-1 policy / canonical RC](https://github.com/masarray/arsonmix/issues/837) and live AGENTS.md; canonical RC exact HEAD and own base/HEAD at reconciliation:
- Active exact-SHA qualification checked; no duplicate FAT or unrelated candidate churn; if superseding, observed blocker and reason:
- Preserved/deferred work and shared-path regression evidence (no stale whole-file overwrite):

- Current `main` / related implementation inspected (SHA or link, if relevant):
- Existing accepted GUI/audio/session behavior to preserve:
- Overlap with other PRs, if any, and how resulting Git changes were reconciled:

**Anyone may work on any file on their own branch.** An open/draft PR, previous file owner, workstream label, missing external thread permission or same-file overlap never blocks starting a change. Resolve actual Git conflicts and verify behavior **at merge time**; see [Non-Blocking Parallel Development](../docs/architecture/PARALLEL_THREAD_CONVERGENCE_20261118.md) and [AGENTS.md](../AGENTS.md). The accepted modular MVVM direction is in the [merged MVVM ADR](../docs/architecture/ADR_MODULAR_MVVM_FEASIBILITY_20261118.md) from [PR #763](https://github.com/masarray/arsonmix/pull/763), not a second competing ADR.

## Task completion for the nontechnical operator (Indonesian — update on every meaningful milestone)
- **Hasil:** what truly changed for the user, and what is NOT implemented?
- **Status:** DESAIN SAJA / KODE DIBUAT / KODE DIUJI / MERGED MAIN / INSTALLER TERSEDIA / HARDWARE TERUJI / DIRILIS
- **Aman dipakai?** YA / BELUM / TIDAK — exact compiled build when applicable
- **Tindakan Anda:** TIDAK PERLU MELAKUKAN APA-APA, or **one safe explicit test/install action** with link
- **Bukti:** PR/HEAD SHA, tests PASS/FAIL/NOT RUN, actual Windows/physical FAT PENDING as applicable
- **Selanjutnya:** what another thread can independently build on, without waiting for an owner lock

Write this summary also in the **final response of the ChatGPT thread doing the work**, even if the task failed or only partially completed. For significant source milestones, retain it in the PR body/comment so future threads can find it. See [owner-friendly handoff](../docs/architecture/OWNER_FRIENDLY_TASK_HANDOFF_AND_MULTI_IO_20261118.md). A comment is communication, NOT CI admission.

## Verification (report only applicable evidence)
- Current PR HEAD / relevant tests executed and result:
- Approved `/preview/` and native ACK/PCM/.smix regression, if affected:
- Installer compiled FX catalog and asset parity, if releasing:
- Physical ASIO/vendor test: PASS / FAIL / PENDING / NOT APPLICABLE:
- Known limitations and rollback:

No new coordination CI is required by this template. Do not confuse source PASS with physical FAT, a logical ACK with installed PCM, or merged code with a published installer. Preserve the real-time render authority and approved GUI.
