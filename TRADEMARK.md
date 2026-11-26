# ArSonMix project identity and branding policy

The official project is maintained through [masarray/arsonmix](https://github.com/masarray/arsonmix). The canonical name is **ArSonMix** and the approved app mark is documented in [assets/icon/README.md](assets/icon/README.md). This policy describes authentic project identity and non-misleading presentation; it does not claim a registered trademark, exclusive rights in generic terms, or new copyright restrictions.

## Official channels

- Source, contribution history, and issue tracker: https://github.com/masarray/arsonmix
- Official published release assets and notes: https://github.com/masarray/arsonmix/releases
- Project website: https://masarray.github.io/arsonmix/
- Approved visual mixer preview: https://masarray.github.io/arsonmix/preview/

Other sites, mirrors, packages or forks are not automatically official. A mirror may accurately identify an unmodified upstream release but should link to its original source and distinguish its own operator. A modified version should identify the modifier, source repository, and modification; do not suggest endorsement, authorship or official release status that does not exist.

## Permitted use

Accurate references, reviews, tutorials, screenshots, attribution, compatibility descriptions and lawful redistribution or modification under applicable licenses are welcome. Forks, commercial redistribution and independent development permitted by AGPL are not prohibited by this policy. For clarity, use a distinct product identity for modified distributions and describe the relationship to upstream accurately. This is a request against confusion, not an added condition on rights granted by AGPL or other applicable licenses.

Do not impersonate the maintainer, publish modified binaries as official builds, fabricate signature/approval status, or claim third-party endorsement. Respect third-party names, logos and artwork under their own terms. See [COPYRIGHT.md](COPYRIGHT.md).

## Verify a Windows installer

From the official release page, download the desired Setup.exe (or ZIP) and SHA256SUMS.txt for the same tag. For example, from PowerShell in that download directory:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\ArSonMix-v0.5.0-alpha.32-Windows-x64-Setup.exe'
```

Compare every hexadecimal digit against the corresponding exact filename in the official SHA256SUMS.txt. For a different release replace the filename. If verifying a ZIP, hash the ZIP using its own checksum. If the hash differs or the source is not trusted, do not run it. SHA-256 and provenance JSON from the same release channel do not independently authenticate the publisher. Do not interpret unsigned status as proof of malware or ignore Defender/SmartScreen warnings; trusted code signing is not currently a mandatory release prerequisite.

Suspected impersonation: report the public URL, claimed version, and non-sensitive evidence to the repository maintainer. For suspected malicious binaries or vulnerabilities, follow [SECURITY.md](SECURITY.md). Do not upload executable samples publicly.
