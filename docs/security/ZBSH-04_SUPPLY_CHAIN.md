# ZBSH-04 — Secure CI and supply chain

Base 7160e6bdef67240001726fa1ba888787f665c6cd. Pin direct external Actions to full upstream commit IDs, resolving the already-used major tag rather than introducing a new major version. Checkout of JUCE 8.0.15 is now its exact commit 91ad83ae34a81e0833b1a2b0866f54846370ae53. Existing repository-audit checkout was already pinned and remains unchanged. The source contract rejects mutable direct action refs, missing workflow-level permission declarations and JUCE/lock drift. It is a line-based guard for the project's current YAML style, not a complete YAML security analyzer or proof of safe transitive action code. GitHub's parser remains authoritative.

## Permission review and scope

- Normal test workflows retain explicit contents read; logical-mixer-fast now also declares it. Required status publisher, owner CI/release dispatchers, Pages deploy and FactoryFX proposal bot retain the permissions needed for their existing operations. Existing owner authorization, exact source SHA checks and merge gate unchanged.
- Release publisher still combines build and publication with contents/actions write. Split trusted publication from build, remove persistent checkout credentials where safely proven, constrain trusted release refs, attest artifacts and reconcile immutable/multi-platform releases in ZBSH-05. No claim of full least privilege yet.
- Dependabot GitHub Actions monthly group, max two open PRs; dependency updates require normal protected CI. Do not auto-merge unreviewed dependencies. Native GitHub secret scanning/push protection and alerts require ZBSH-03 owner verification. No private alert access or historical secret scan in this milestone.
- Source inventory is config/supply-chain-baseline.json, with known upstream refs and explicit unknowns. It is NOT an exact binary SPDX/CycloneDX SBOM; derive that from actual staged build files in ZBSH-05. Existing FactoryFX and VB-CABLE hash gates preserved. Unlocked package/tool/runner and transitive action dependencies remain open.
- Existing fast native, UI and release tests must run on the changed workflow scope, plus supply-chain regression on all workflow changes. No extra manual Windows/FAT dispatch for this CI-only patch; existing automatic main qualification may run according to existing policy. No relaxation of required checks.
- No runtime changes, no paid services or extra latency. CodeQL/sanitizers and comprehensive vulnerability scanning in ZBSH-07; authenticated distribution and build/publish split in ZBSH-05.

## Verification and maintenance

Run node --test tests/supply_chain_audit.test.mjs and node scripts/supply_chain_audit.mjs. Update the inventory when a reviewed JUCE revision changes. Dependabot PRs must keep full SHA pins. Read [GitHub security hardening](https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions), [Dependabot options](https://docs.github.com/en/code-security/dependabot/working-with-dependabot/dependabot-options-reference). A passing source guard does not establish absence of vulnerabilities. Revert a broken pin via protected PR, not mutable tags.
