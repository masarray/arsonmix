# ZBSH-01 — Zero-budget security baseline and threat model

Audit date: 2026-11-26 (Asia/Jakarta). Scope: public ArSonMix Windows desktop mixer, its official source/build/release chain, local data, native VST3 and WebView control boundaries. Baseline source commit: `2253001926e6ef318eb00c9673417dc1177df4d6`. Release metadata sampled: v0.5.0-alpha.30 through alpha.32. GitHub settings evidence is point-in-time, not versioned in Git.

## 1. Decision and scope

ArSonMix remains AGPL-3.0-only, offline-first, free of mandatory account/activation/DRM, with no paid code signing or trademark registration prerequisite. Retain third-party license obligations. Optional zero-cost signing may be evaluated independently. No bypass of SmartScreen/Defender, no self-signed root installation on user machines. Preserve the approved GUI, direct primary ASIO path and single accepted render graph. All security I/O, cryptography, parsing, logging and process management belong outside the real-time audio callback. No extra FIFO or lookahead in the primary ASIO path as a security feature.

This is a bounded source/configuration review, not a penetration test, full-history secret scan, binary verification, complete code review, SBOM, or hardware qualification. Findings describe control coverage and remaining work, not confirmed exploitable vulnerabilities. Do not publish credentials, working exploit reproductions, or sensitive incident details in public issues.

## 2. Evidence inventory

All paths below refer to the audited commit unless explicitly noted. Reproduce source inspection by checking out that commit. Evidence levels: **Observed** = directly read source or GitHub API; **Partial** = observed control but end-to-end enforcement or completeness untested; **Unknown** = inaccessible or not reviewed. Never interpret unknown as a pass.

| Area | Evidence | Result |
|---|---|---|
| Governance | [LICENSE](../../LICENSE), [LICENSE-NOTICE.txt](../../LICENSE-NOTICE.txt) | Observed AGPL-3.0-only notice, existing contributor and third-party attributions; asset rights not independently verified. |
| Repository | [main ruleset](https://github.com/masarray/arsonmix/rules/26568033), branches/main API | Observed active main ruleset, deletion/non-fast-forward restrictions, required PR, strict status check ArSonMix / Required CI Merge Gate, zero mandatory approvals, no bypass actors listed. Legacy branch protection endpoint returned 403. |
| CI authority | [.github/workflows/required-pr-merge-gate.yml](../../.github/workflows/required-pr-merge-gate.yml), [scripts/required_pr_merge_gate.mjs](../../scripts/required_pr_merge_gate.mjs) | Partial: privileged status publisher checks out default branch, read-only contract job, separate status-write authority; exact-head run logic present. Reviewed source, did not run adversarial event tests. |
| Dispatch | [.github/workflows/chat-ci-milestone.yml](../../.github/workflows/chat-ci-milestone.yml) | Partial: owner login and association check, default-branch checkout, persist-credentials false. Full dispatcher authorization not exhaustively audited. |
| Release build | [.github/workflows/alpha-preview-publish.yml](../../.github/workflows/alpha-preview-publish.yml) | Partial: source SHA and release branch identity checks, version and notes checks, hash generation, refusal to replace existing release, write permissions for publication/Pages dispatch. Third-party Actions use version tags and JUCE uses release tag. |
| Published releases | [alpha.32](https://github.com/masarray/arsonmix/releases/tag/v0.5.0-alpha.32), GitHub releases API | Observed alpha.30–32 prereleases, immutable=false, portable/setup/ZIP and SHA256SUMS, BUILD_PROVENANCE, INSTALLER_PROVENANCE, FACTORYFX_STAGE_REPORT assets with API digest metadata. /releases/latest returned 404; this does not imply absent prereleases. No actual artifact bytes/signature verified. |
| Updater | [installer/ArSonMixAutoUpdate.ps1](../../installer/ArSonMixAutoUpdate.ps1) | Partial: canonical repository asset URL/name, bounded bytes, redirect checks, SHA-256, source/digest correlation, staged file validation and transaction approval. These are consistency/integrity checks, not independently authenticated publisher proof. |
| Installer | [installer/ArSonMix.iss](../../installer/ArSonMix.iss) | Partial: lowest privilege default, exact-process shutdown and locked-EXE preflight; PowerShell helpers and optional prerequisites are separate execution boundaries. No installer execution audit. |
| FactoryFX | [scripts/factoryfx_release_bundle.py](../../scripts/factoryfx_release_bundle.py), [src/FactoryFxRegistry.h](../../src/FactoryFxRegistry.h) | Partial: approved artifact SHA, bounded archive intake, symlink rejection and runtime manifest/binary digest checks. Full ZIP/Windows filesystem behavior untested. |
| User VST3 | [src/PluginVault.h](../../src/PluginVault.h), [src/PluginCatalog.h](../../src/PluginCatalog.h), [src/StartupVst3RecoveryGuard.h](../../src/StartupVst3RecoveryGuard.h) | Partial: isolated vault directories, reparse checks, non-audio scanning and startup restore crash marker. A worker thread is not a process security boundary. |
| Local project data | [src/NativeMixerSessionStore.h](../../src/NativeMixerSessionStore.h), [src/BuiltInFxUserPresetStore.h](../../src/BuiltInFxUserPresetStore.h) | Partial: size/count bounds, hash checks, JSON schema/shape checks and atomic generation/preset persistence. Adversarial inputs and filesystem behavior not tested here. |
| Native control bridge | [src/ConsoleLiveBridgeV6.h](../../src/ConsoleLiveBridgeV6.h) | Partial: typed IDs and payload checks, CONTROL thread ownership. WebView origin/navigation policy, message budget and native command allowlist need dedicated review. |
| Security automation | root and .github directory listing; workflow inventory | Observed no SECURITY.md in root/.github, no .github/dependabot.yml, no dedicated CodeQL workflow in snapshot. Repository-level default setup, private reporting and alert settings unknown. |

## 3. Threat model and trust boundaries

Assets: maintainer identity and credentials; protected history/tags and workflow definitions; release artifacts and source correspondence; update channel; microphone/audio/session data; machine integrity; live performance continuity.

Actors: legitimate contributor or fork; compromised maintainer/dependency/action; untrusted plugin/preset/session author; untrusted download or local process. Assume OS/user account itself may be compromised: a desktop app cannot guarantee protection against a malicious administrator or native code running with the same privileges.

| Boundary | Threat | Required invariant |
|---|---|---|
| Public contribution → trusted main | unauthorized change or review bypass | PR-only history, exact-head trusted checks, intentional owner authority; no privileged execution of untrusted PR code. |
| Dependencies → build runner | dependency or action substitution | reviewed immutable pins, least privilege, clean build; provenance traces exact inputs. |
| Build → publication → user | tampered/replaced artifacts or impersonation | independent origin authentication plus hash verification; official channel, tag/SHA linkage, immutable publication where feasible. |
| Release → updater | untrusted metadata, replay, replaced staged file | authenticated manifest tied to artifact/version/source, reject invalid or disallowed rollback, reverify before apply, preserve known-good install. |
| VST3/prerequisite → native host | arbitrary native execution, crash/hang | explicit trust boundary, bounded scan worker process and recovery; never advertise scanner isolation as runtime sandboxing. |
| Session/preset/WebView → CONTROL | invalid/oversized input, unexpected filesystem or privileged native access | schema and size limits, explicit allowed operations, canonical paths, atomic state and fail-closed handling. |
| CONTROL → render callback | blocking or overload disrupts live performance | no added security work, locks, disk/network access or buffering in primary callback; safe publication at existing boundaries. |
| Local diagnostics → public issue | leaked credentials, paths, session/audio data | redaction and opt-in sharing; no mandatory telemetry. |

## 4. Risk register and implementation gates

P0 = prioritize before declaring trusted distribution/security baseline complete; P1 = next hardening; P2 = optional enhancement. These are engineering priorities, not CVSS scores. No exploitability claim. Status open unless explicitly verified closed.

| ID | Priority | Control gap / acceptance criterion | Owner milestone |
|---|---|---|---|
| SEC-001 | P0 | Independent release authenticity not established. Generate and verify GitHub/Sigstore build attestation for final installer and archives against official repository, trusted workflow and expected source; negative test mismatched artifact/identity. Retain SHA256SUMS. | ZBSH-05 |
| SEC-002 | P0 | Version-tag Actions and publisher authority need hardening. Review all workflow triggers and permissions, pin external Actions to reviewed full SHAs, default read-only, separate trusted build and publication privileges, preserve exact-head CI gate; fork PR negative tests. | ZBSH-04 |
| SEC-003 | P0 | Sampled releases mutable in API; main ruleset only observed. Protect release branches/tags and evaluate immutable release settings; prove unauthorized tag rewrite blocked and exact-release identity. Avoid changing historical releases. | ZBSH-03/05 |
| SEC-004 | P0 | Updater source proves integrity checks but not independent publisher proof. Define trust bootstrap, keyless or signed-manifest verification compatible with Windows offline use, explicit version/recovery policy, tamper/replay tests and no apply during active session. No automatic weakening of existing updater. | ZBSH-08 |
| SEC-005 | P1 | User plugin scanner is in-process. Introduce separately launched, bounded, crash-tested scanner, timeout and quarantine; no forced retry. Evaluate runtime isolation separately with measured latency and explicit compatibility. | ZBSH-06/09 |
| SEC-006 | P1 | Dependency inventory/pin completeness and component redistribution rights unverified. Inventory JUCE/SDKs/FactoryFX/driver/WebView/assets; lock exact upstream hashes, document license and produce SBOM. | ZBSH-02/04 |
| SEC-007 | P1 | No repository SECURITY.md observed. Establish supported-version scope and confidential reporting channel; enable private reporting if available and document coordinated disclosure without promising SLA. | ZBSH-02/03 |
| SEC-008 | P1 | No dedicated CodeQL or Dependabot configuration observed. Verify default setup/alerts, introduce free targeted C++/dependency/secret checks with actionable triage; scan history privately and rotate exposed credentials if found. | ZBSH-03/04/07 |
| SEC-009 | P1 | Existing input limits not adversarially qualified. Add sanitizer/fuzzer/negative fixtures for session, preset, archive, plugin metadata, native bridge and update transaction; prove bounded failure and unchanged valid behavior. | ZBSH-07/08/10 |
| SEC-010 | P0 | Account security, app/token scopes, secrets and backup unverified. Owner verifies 2FA/passkey/recovery, least privilege, secrets inventory without exposing values, independent backup and restore test. Review before changing access. | ZBSH-03/11 |
| SEC-011 | P1 | Privacy and WebView boundary incomplete review. Confirm no remote native bridge exposure, redact logs and diagnostics, no automatic audio/session uploads; negative origin and message validation tests. | ZBSH-08/10 |
| SEC-012 | P2 | Paid Authenticode and DJKI out of scope. Optional free signing eligibility evaluation; publish branding/copyright policy without implying trademark registration or restricting AGPL rights. | ZBSH-02/12 |

## 5. Ordered roadmap (zero incremental paid-service budget)

1. ZBSH-02: ownership, third-party compliance, SECURITY.md and official distribution identity; retain existing LICENSE-NOTICE.
2. ZBSH-03: owner account/settings checklist, repository/branch/tag controls and recovery plan; preserve solo-maintainer workflow, do not impose an unavailable second reviewer.
3. ZBSH-04: trusted CI/supply chain, immutable reviewed pins, least privilege, static/secret/dependency checks and SBOM.
4. ZBSH-05: artifact attestations and reproducible verification instructions; preserve existing checksum/provenance contract. Sigstore is not Windows Authenticode.
5. ZBSH-06–10: native host isolation, memory safety, authenticated updates/input safety, fault recovery, regression and measured RT qualification.
6. ZBSH-11–12: backup/restore drill and optional no-cost signing. No paid certificate, paid security service, mandatory online account, or trademark registration gate.

## 6. Owner-only evidence checklist (not completed by this audit)

- Verify 2FA/passkeys and safely stored recovery codes; review active sessions and installed apps.
- Inspect [rulesets](https://github.com/masarray/arsonmix/settings/rules), release/tag settings and environment protection; record non-sensitive decisions.
- Inspect Actions default permissions, allowed actions and fork policies; inspect secrets names/scopes only, never values.
- Inspect security settings (private vulnerability reporting, Dependabot, secret scanning/push protection, CodeQL default setup). Record unavailable features explicitly.
- Make an independent repository/release backup and prove restore to a safe test location.

## 7. Completion and qualification

ZBSH-01 delivers a source/configuration baseline, explicit evidence limitations, threat model and prioritized risk register. It does not change runtime, GUI, installer, workflows, release, or settings. No Windows/Chromium/FAT required or dispatched for this documentation-only scope. Current protected merge checks remain authoritative. Security qualification remains incomplete: no full-history secrets/dependency scan, no runtime/binary or hardware tests, no settings requiring admin access, no actual vulnerability remediation. Re-audit touched boundaries at the exact SHA of each implementation milestone; do not treat this snapshot as a permanent certificate.

References for implementation: [GitHub Actions security hardening](https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions), [artifact attestations](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations), [OpenSSF Scorecard](https://scorecard.dev/), [Sigstore](https://www.sigstore.dev/). These are implementation references, not claims that external verification was performed during this source audit.
