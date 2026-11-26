# ZBSH-FINAL.2 — Low-friction secure development
Keep the existing single required status, zero required reviewers, protected main and owner-only exact-SHA publication. Do not bypass a red gate. Ordinary branch iteration is not a public release.
 
- CHECK: scoped fast checks only. FAST: field portable if needed, never full evidence. FEATURE: existing narrow approved native scope.
- FULL: exact-head merge qualification, no installer request. RESUME: same scope as FULL, repeatable for missing lanes after a timed-out/partial dispatcher; never automatically re-dispatch a known failed exact-head UI/native run. Use explicit GitHub retry for a diagnosed runner failure or fix source. Existing owner checks, same-repo, head and actual-run dedup guards remain.
- QUALIFY: choose this from the start when an installer for physical testing is requested. Repeat the same command if dispatcher timed out before dispatch; both QUALIFY and RESUME wait briefly for just-created fast jobs and consult actual run identity, never automatically retry a known red full lane. Full Chromium if relevant and FAT instead of a separate native-only build; FAT is existing full CTest plus installer, never public release. Avoid requesting FULL followed by QUALIFY unless an installer is newly needed. Existing FAT guards and scope rules remain.
- Public release still requires reviewed source, existing release controller, final exact-byte attestation and verified publication. No unsigned/self-signed bypass.
- Pixel parity retains exact bytes; new diagnostics upload the two actual screenshots on mismatch so root cause can be inspected without speculative changes. No masking or retry-as-pass.
- Owner can enable GitHub auto-merge in Settings > General > Pull Requests (currently disabled); opt in per ready PR. Do not disable protection. Settings administration is not provided by the connected API.
 
Commands are owner PR comments using /arsonmix-ci resume SHA or /arsonmix-ci qualify SHA, substituting the exact current 40-hex head. These new commands are active only once the trusted dispatcher source is merged to main. During staging, existing commands continue to work.
