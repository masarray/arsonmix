# ZBSH-11 — Disaster recovery (zero budget)

## Source and release archive
Run from a trusted clean checkout using Python 3.11+, Git, and optionally the official authenticated GitHub CLI (read access only). Choose a NEW path on an independently controlled disk; its parent must exist.

```powershell
python scripts/disaster_recovery.py backup E:/ArSonMix-DR-20261126 --releases
python scripts/disaster_recovery.py verify E:/ArSonMix-DR-20261126
python scripts/disaster_recovery.py restore E:/ArSonMix-DR-20261126 D:/ArSonMix-Recovery-Test.git
git -C D:/ArSonMix-Recovery-Test.git log -5 --oneline
```

Omit --releases for a source-only backup. The script clones advertised refs, checks Git objects, creates a standalone bundle, hashes every archived file, and performs an actual offline bare-clone/ref comparison before publishing the backup folder. A restore always targets a new bare repository, has no remote configured, and never publishes or executes recovered code. Release mode archives non-draft release metadata and asset bytes under numeric IDs; published SHA-256 is checked when supplied. Release assets are NOT authenticated by the backup manifest; verify official attestation separately before executing any installer. Do not re-upload automatically.

## Independent copies and owner duties
- Keep at least two independent copies, one disconnected; do not rely on a branch, GitHub Actions artifact, or GitHub account as the only backup. Run after accepted releases and at least weekly while active. Verify and perform a restore drill monthly. Suggested objectives: source RPO 7 days (or after release), RTO 1 working day, not yet measured against production.
- Preserve owner recovery codes offline, separately from repository data. Never archive credentials, tokens, GitHub secrets, or user Studio into public GitHub. Export repository settings/rulesets, issue/PR discussions and LFS/submodule objects separately if needed. Git bundle includes only fetched refs and Git objects, not GitHub settings, private advisories, Actions artifacts, or LFS payloads. Public release archive excludes drafts and does not archive GitHub-hosted attestations.
- During incident: contain compromised access, rotate credentials, preserve evidence, compare backup manifest digest against an independently retained trusted digest, restore locally, review before creating a new remote and re-establish protected rulesets, CI and verified release provenance. Never force-push over main.
- Source/release archive has no scheduled upload or recurring remote workflow; owner must store it on independent media. SHA-256 detects corruption, not malicious replacement of both manifest and bytes. Filesystem race protection assumes owner-controlled offline destination.

## Studio cold recovery
Save native Shows, snapshots and presets, mute output, and avoid editing while using existing Backup Studio to USB/Folder. Close ArSonMix before copying verified backup to a NEW local folder. Preserve the original. Review copied EXE and VST binaries independently; backup verification is not publisher authentication. Configure physical driver/device and keep output muted until checked. Native fixture exercises copying a verified portable workspace and verifying recovered bytes; no restore-in-place or automatic plugin execution is introduced. Browser-only data, machine routing, external sample libraries and plugin activation remain separate. Backup is per-file verified, not an atomic snapshot of a changing live Studio. No new real-time callback work.

## Evidence gates
CI runs offline Git recovery, corruption, no-overwrite, interrupted staging and release digest fixture tests, plus native Studio manifest/cold copy tests and existing security regression. Actual independent production backup, public release asset download drill, Windows hardware recovery and owner account recovery remain separate acceptance evidence. Do not claim them from fixture success.
