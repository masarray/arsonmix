# ArSonMix open-source native test releases — Windows, Linux and macOS

**Policy:** three genuine JUCE desktop targets, one exact built-in /preview/ GUI, one PCM routing authority. Shipping the Windows EXE as a renamed Mac/Linux download is forbidden. First-party FactoryFX and licensed commercial vendor VST3 are optional and **not bundled** unless approved upstream.

## No commercial signing cost

| OS | Public-test asset | Signing/distribution | First-run guidance |
| --- | --- | --- | --- |
| Windows x64 | `Windows-x64-Setup.exe` and `Windows-x64-Portable.zip` | Existing verified Inno Setup pipeline; unchanged | Download from official landing; always verify published SHA256. |
| Linux x86_64 | `Linux-x86_64.tar.gz` containing a native ELF | No commercial signing | Extract; `chmod +x ArSonMix` if necessary. Requires Ubuntu 24.04-compatible GTK3, WebKitGTK 4.1, ALSA/JACK system libraries. |
| macOS x64 (or arm64 if the native runner is arm64) | `macOS-x64.dmg` or `macOS-arm64.dmg` with native JUCE .app | FREE **ad-hoc codesign** `codesign --sign -`; **no** paid Apple Developer ID, certificate or notarization | Expect possible Gatekeeper warning. Verify SHA256 and trusted source; try opening, then **System Settings → Privacy & Security → Open Anyway** if comfortable. **Do not disable Gatekeeper globally.** |

An ad-hoc signature only satisfies code-integrity requirements; it does **not** authenticate the author or provide Apple notarization. Some configurations may still block execution. No claims of a universally seamless installation, Apple certification, or physical hardware audio FAT.

## One owner comment = at most one native OS runner

The only dispatch surface is issue [#450](https://github.com/masarray/arsonmix/issues/450), with an authenticated repository OWNER comment. Existing Windows release command remains unchanged:

```text
/arsonmix-release v0.5.0-alpha.5 <exact-40-character-releases/alpha/v0.5.0-alpha.5-HEAD-SHA>
```

For portability and packaging qualification **without publishing**, on a development branch:

```text
/arsonmix-port linux feat/native-linux-macos-owner-release-20261108 <exact-current-branch-HEAD-SHA>
/arsonmix-port macos feat/native-linux-macos-owner-release-20261108 <exact-current-branch-HEAD-SHA>
```

After Windows alpha.5 exists at **the identical SHA** and both source and release notes are reconciled, an agent may request ONE public OS build+publish per platform:

```text
/arsonmix-publish linux v0.5.0-alpha.5 <exact-40-character-Windows-release-SHA>
/arsonmix-publish macos v0.5.0-alpha.5 <exact-40-character-Windows-release-SHA>
```

The controller checks exact release branch, HEAD lease, OWNER identity, deduplicated comment and existing same-platform workflow runs. A different-SHA release, any prior platform asset, or a failed identical-SHA build **cannot be rebuilt blindly**. It requires a real fix and a new exact-SHA release candidate. There is **no push, cron, PR-ready, OS matrix or nightly production runner**. Main branch safety checks continue as normal, but no three-platform release job launches on ordinary commits.

## Release gates and end-user experience

1. Static portability and owner-auth contracts run cheaply in the PR. The code maps Windows to WebView2 and ASIO/WASAPI, macOS to WKWebView and CoreAudio, and Linux to WebKitGTK with ALSA/JACK. This is **one** embedded approved GUI and **one** JUCE device/callback authority.
2. OS-specific one-time native CMake configure/build and full CTest on pinned JUCE 8.0.15. This requires actual source compilation, not a source-only simulation or Windows binary.
3. Linux packaging verifies the executable is an actual x86_64 ELF, checks dynamic libraries, and creates a TAR archive with SHA256 and `LINUX_PROVENANCE.json`. macOS packaging verifies a Mach-O .app/Info.plist, applies a free ad-hoc signature, checks it, verifies a real DMG, and writes `MACOS_PROVENANCE.json`. Platform-specific checksum manifests preserve the immutable Windows `SHA256SUMS.txt`.
4. Source-identity gate requires **the Windows public release already published** and its immutable target SHA exactly matching the compiled OS source. Never replace existing assets; append only real native assets and OS provenance to the same prerelease.
5. Refresh the same-origin `release-catalog.json` on the official landing site only after successful upload. Browser links download direct immutable GitHub binaries; unsupported OS remains unavailable. A Pages refresh problem is not a reason to rebuild an already published OS binary.
6. **Physical user audio FAT remains pending** until humans test actual interfaces, CoreAudio/ALSA routing, FX behavior, recovery and signal monitoring. Alpha is experimental; don't use for critical live performances.

## CI cost contract

**Development:** small fast Ubuntu contracts on relevant PR changes. **Major port milestone:** one opt-in Linux full-native job; independently one macOS job only if warranted. **Public distribution:** after same-source Windows release, exactly one owner-comment OS publisher per platform; no extra qualification rebuild for the same public SHA. One Pages refresh per real asset upload, not on every source commit.

If native compilation, runtime dependencies, notarization-sensitive operation, packaging, integrity or release upload fails, report the exact failing step honestly and keep its landing download disabled. Never lower the gate to claim a successful release.
