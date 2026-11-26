# ZBSH-05 — Trusted release and artifact verification

Source milestone based on ZBSH-04. No commercial signing, paid service, DJKI, runtime license, mandatory account or new audio path. This is source/CI implementation, not evidence that a public installer is already attested.

## Architecture

Windows and OS build jobs have contents: read, no publishing/OIDC permission, and checkout credentials are not persisted. Existing native/package/installer qualification remains. The isolated publisher downloads only the named same-run artifact, recomputes exact allowlisted release asset hashes and provenance, checks source SHA/branch, then requests GitHub build provenance for final files and verifies every subject against the official repo, workflow, source ref, source and signer SHA and GitHub-hosted runner identity before publishing. Publisher never executes the installer. It does execute the reviewed candidate verification script and workflow: protection depends on maintainer account, reviewed release commit and trusted GitHub runner; this is not a SLSA level 3 claim.

Windows uses draft upload then publish; a failed draft is retained for explicit owner inspection, never overwritten or silently retried. Existing Windows setup EXE, setup ZIP, portable ZIP and legacy checksum/provenance names remain; new RELEASE_TRUST-windows.json binds the exact seven original assets and actual portable ZIP file hashes, not a complete dependency SBOM. Linux/macOS retain current binary and checksum names plus their own RELEASE_TRUST files. No historical assets modified. Qualification-only OS builds do not publish or request attestations.

## User verification (no paid certificate)

Download from https://github.com/masarray/arsonmix/releases. Install official GitHub CLI if needed. Obtain expected tag and exact release commit from the protected official release history independently of downloaded file. Verify the exact downloaded artifact before executing:

```powershell
gh attestation verify .\ArSonMix-vX.Y.Z-alpha.N-Windows-x64-Setup.exe --repo masarray/arsonmix --signer-workflow masarray/arsonmix/.github/workflows/alpha-preview-publish.yml --source-ref refs/heads/releases/alpha/vX.Y.Z-alpha.N --source-digest EXPECTED_40_CHAR_SHA --signer-digest EXPECTED_40_CHAR_SHA --deny-self-hosted-runners
```

Replace placeholders. For Linux/macOS use crossplatform-native-qualification.yml as signer workflow and the actual package name. Verify downloaded trust manifest too if using it, not just the hash from an unauthenticated file. Missing attestation/failed identity check means not verified: legacy releases do not retroactively become attested. SHA-256 verifies consistency; GitHub/Sigstore attestation provides cryptographic origin evidence. Neither replaces Authenticode, removes SmartScreen warnings, guarantees malware-free software or proves physical ASIO FAT. Do not turn off OS security. The existing updater is unchanged; mandatory updater authenticity verification and replay policy belong to ZBSH-08.

## Immutable release transition

Draft upload is compatible with a future Windows-only immutable release. Existing optional Linux/macOS append workflow intentionally remains supported on mutable releases and now rejects immutable ones before allocating an OS runner. Do not enable global immutable releases until owner chooses Windows-only or a reviewed all-platform draft assembly/seal flow. This milestone does not change repository settings or historical releases. A fully immutable multiplatform release requires separate staging/finalization design and qualification.

## Qualification

Release fast contracts include negative tamper, wrong source, duplicate checksum and ZIP path tests. A separate main-only synthetic probe uses GitHub attestation service, verifies an unmodified subject and rejects changed bytes; no contents write, no release, no compiler. Actual release job must pass on the next approved exact-SHA release; no test release is published by this PR. Full binary component/dependency SBOM and license verification remain open; file inventory is explicit, not a false SPDX/CycloneDX claim.

References: https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations and https://cli.github.com/manual/gh_attestation_verify.
