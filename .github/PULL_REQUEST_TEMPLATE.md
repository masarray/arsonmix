## Why this work is not duplicated
Current main SHA: `...`
Related issue / existing PRs reviewed: 

## REQUIRED agent ownership (replace CHANGE_ME before opening)
<!-- arsonmix-workstream: CHANGE_ME -->
<!-- arsonmix-issue: #397 -->

Allowed workstream: GUI / ENGINE / INTEGRATOR / FAT.
Only the owning lane edits each production file. See [coordination #397](https://github.com/masarray/arsonmix/issues/397) and [workstream rules](../docs/workstreams/MULTI_AGENT_COORDINATION.md).

## Meaningful implementation and handoff
- Owned files changed:
- Existing implementation reused (PR/SHA):
- Native producer/consumer ABI change (if any):
- Exact-head targeted CI URL and demonstrated behavior:
- Real PCM/GUI/physical FAT proof or **PENDING**:
- Next blocked dependency + receiving workstream:

Do not create another GUI, PCM engine, duplicate routing graph, or second CI workflow. Avoid changing CMake/src/Main.cpp outside INTEGRATOR; test evidence is not an audio-LIVE claim.

If this older PR has been parked pending reconciliation, include:
<!-- arsonmix-coordination: parked -->
Remove the parked marker only after an owner-scoped diff and conflict audit.
