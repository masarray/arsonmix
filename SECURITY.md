# Security policy

ArSonMix is an open-source desktop audio mixer. Security issues are welcome; this project is maintained on a volunteer, best-effort basis, without a paid support or response-time commitment.

## Supported versions

The latest official Windows alpha/prerelease and current protected main are the active investigation targets. See [official releases](https://github.com/masarray/arsonmix/releases). Alpha builds are not long-term-support releases. Older releases may receive fixes at maintainer discretion; report the exact version even if it is older. Browser preview is not native audio qualification.

## Report a vulnerability privately

1. Open the [GitHub security advisories page](https://github.com/masarray/arsonmix/security/advisories). If **Report a vulnerability** is available, submit a private report there.
2. If private reporting is unavailable, open a [normal issue](https://github.com/masarray/arsonmix/issues/new) asking ONLY for a private security contact. Do not disclose the vulnerability, affected paths, exploit, attachment or other sensitive details in the public issue. Wait for a confirmed confidential channel.
3. For ordinary reproducible bugs without security impact, use the normal issue templates.

Include privately: affected version and exact installer/source SHA if known, OS, conditions, impact, minimal safe reproduction, and any suggested fix. Use dummy sessions and synthetic audio. Remove credentials, personal names/paths, microphone recordings, session files containing personal data and vendor secrets. Do not send executable attachments unless a maintainer explicitly agrees to a safe transfer.

Please test only your own systems or systems for which you have authorization. Do not access another user's data, disrupt live sessions, or test against production distribution infrastructure without permission.

## Handling and coordinated disclosure

Maintainer triages impact and scope, reproduces safely, tracks sensitive details privately, prepares a fix and targeted negative tests, and publishes release/advisory information when appropriate. Coordinate timing with the reporter; there is no guaranteed SLA, bounty or blanket legal immunity. Acknowledgement is optional at reporter request. Do not publish live credentials or exploit detail before a fix is available.

Security processing must stay outside the real-time audio callback. No DRM, mandatory online activation, or paid signing requirement. The absence of an advisory does not mean a release is vulnerability-free.

## Trust official downloads

Use the [official project repository](https://github.com/masarray/arsonmix) and [release page](https://github.com/masarray/arsonmix/releases). See [TRADEMARK.md](TRADEMARK.md) for official identity and the verification procedure. Hashes establish integrity relative to trusted metadata; they are not a Windows trusted-publisher signature. Never disable OS protections to run an unknown installer.

Maintainer setup: enable [GitHub private vulnerability reporting](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository) if not already enabled. The presence of this document does not prove the setting is enabled.

Repository maintainer: see [ZBSH-03 administrator runbook](docs/security/ZBSH-03_REPOSITORY_HARDENING.md) for free repository protections, configuration evidence and recovery precautions. The public ruleset audit is read-only; it cannot confirm private settings or substitute for push protection.
