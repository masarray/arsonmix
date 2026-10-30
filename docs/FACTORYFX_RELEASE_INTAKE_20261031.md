# FXF2 — First-party FactoryFX release intake, promotion and Windows packaging
Date: 2026-10-31. Implementation branch: `feat/fxf2-crossrepo-release-intake-20261031`.
Owners: issue #185, canonical ADR `ADR_FACTORY_FX_BUNDLE_20261030.md` and PRD `PRD_FACTORY_FX_20261030.md`.

## Product behavior, without another DSP host
ArSonMix is a LIVE Windows ASIO/WASAPI VST3 mixer, not an audio editor.
Keep `ArSonEQ`, `ArSonVerb` and `ArSonKuPik Enhancer` as separately
built/signed-or-attested VST3 packages from their own repositories. FXF2
does not fork algorithms, copy JUCE9 into JUCE8, create a parallel audio
callback, add Web Audio effects or replace L2 / all-channel FX Rack.
The existing native `PluginCatalog` + `FactoryFxRegistry` discover
approved adjacent `FactoryFX` bundles off the RT thread. User VSTs remain
in the independent portable Vault. Existing bypass, routing ACK, Show
and native VST editor lifecycle stay the only processing path.

## Release model: every upstream release is *detected*, then admitted safely
1. A tiny daily/dispatch `factoryfx-release-watch.yml` checks the latest
   public GitHub Release for `masarray/arson-eq`, `masarray/arsonverb`
   and `masarray/vst-enhancer` (the published binary distributor for
   private DSP source `masarray/askp-vst`). It proposes an automated PR
   updating `config/factoryfx-upstream-candidates.json`. No Windows build,
   asset download, branch mutation of application code, or plugin promotion.
   The public GitHub Release is required: a source branch or Actions
   temporary artifact is not silently promoted as a factory plugin.
2. Inspect the new release's exact source SHA, embedded BUILD provenance,
   Windows x64 VST3 ZIP, whole-bundle class ID, the signed/reviewed
   checksum, original license/JUCE/EULA notices, actual JUCE host
   `PluginDescription.uniqueId`, load/bypass/state/mono-stereo/AUX,
   multi-instance and migration behavior. ArSonMix #15 0FX device FAT
   remains a separate blocker. A title/tag or green CI alone is not proof.
3. Only after explicit legal, entitlement and host acceptance, propose a
   reviewed change to `config/factoryfx-approved-releases.json`.
   Entries must contain EXACT `productId, version, releaseTag, assetName,
   artifactSha256, sourceRepo, sourceCommit, vst3ClassId, hostUniqueId,
   licenseReview=approved, entitlementPolicy, hostQualification=passed`.
   Every item is immutable by artifact SHA, **not latest URL**. For the
   enhancer the source repo is private `masarray/askp-vst`, while the
   binary is always downloaded from public `masarray/vst-enhancer`.
4. Add an explicit reviewed notice file
   `config/factoryfx-notices/<productId>.txt` (license, redistribution
   and activation terms). It is included in `FactoryFX/licenses`.
   No credentials, activation bypass or private DSP source enter ArSonMix.
5. Windows package runs `factoryfx_release_bundle.py stage` only after
   native tests/build; the utility fetches pinned release ZIPs, checks
   downloaded SHA256, allows only the expected Windows x64 VST3 bundle,
   enforces case-unique relative paths, file-count/size budget and
   symlink/traversal rejection, writes per-file SHA256 manifest and
   reviewed notices, then verifies the complete staged directory.
   The independent release ZIP verifier checks this against the lock and
   rehashes every FactoryFX file inside the compressed archive.
   Runtime `FactoryFxRegistry` rechecks per-file SHA256 off RT.
6. New approved version -> new reviewable lock PR -> existing canonical
   Windows CI packages exactly that version, with the native host
   unchanged. Deployment is atomic at whole application-package level.
   Never live-replace a loaded VST3 while audio is processing. Regression
   test older Show/vendor states before release and retain a rollback
   artifact on failed host/CPU/real hardware FAT.

## Commands
- Pure offline intake/integrity tests:
  `python -m unittest discover -s tests -p test_factoryfx_release_bundle.py -v`
- Observe release candidates (public API only):
  `python scripts/factoryfx_upstream_watch.py`
- Produce approved FactoryFX staging after packaging the JUCE EXE:
  `python scripts/factoryfx_release_bundle.py stage --output dist`
- Independently verify staging:
  `python scripts/factoryfx_release_bundle.py verify --output dist`
- Release gate to prevent a missing factory plugin:
  `python scripts/factoryfx_release_bundle.py stage --output dist --require factory`
  or `--require six` for full official six-effect production.

## Accurate current state
At the 2026-10-31 cross-repo read:
- ArSonMix main `9746e3e`: registry/picker merged; Windows and Chromium
  Actions `41071015432` and `41071015423` SUCCESS **for app-only
  existing package**, NOT acoustic hardware FAT or a factory bundle.
- ArSonEQ: public default branch still only LICENSE, no GitHub Release.
  Source on feature branch and 2026-10-30 Actions artifact exhibits
  mismatched embedded BUILD commit; see `masarray/arson-eq#6`.
- ArSonVerb: main has opt-in JUCE VST3 core; no GitHub Release and
  `LICENSE-STATUS.md` explicitly says outbound license unresolved;
  see `masarray/arsonverb#4`.
- ArSonKuPik: published `v0.5.26` Windows x64 VST3 ZIP on
  `masarray/vst-enhancer`. Bundling/entitlement acceptance is still
  open at private source `masarray/askp-vst#60`.
- `config/factoryfx-approved-releases.json` intentionally starts
  with ZERO entries. Thus the current ArSonMix artifact remains app-only;
  NO bundled plugin is claimed until the owners publish and approve
  compatible releases. Do not fill hashes/CIDs/UIDs by guessing.
- Previous canonical six effects and FXF1/FXF2/FXF3 product roadmap
  remain unchanged: EQ/Verb/Enhancer are the first three upstream
  suppliers, Comp/Harmony/Color are separate future releases.
- CI economy: metadata watch is Linux-only, max 5m, and cannot trigger
  the native Windows workflow. Changes to approved lock, release
  packager or notices trigger ONE full existing Windows quality gate;
  the packager's offline tests run first, before JUCE compilation.
  A source artifact is not a hardware acceptance test.

## Handoff / release acceptance
Promotion is DONE only when official ArSonMix ZIP contains
`FactoryFX/factoryfx-manifest.json`, complete VST3 dirs and notices,
per-file hashes pass and Windows native VST3 host reads each approved
factory entry as actually installed with working DSP. Application
portable/install parity, CH1–N multi-insert/bypass, Save/Recall, plugin
latency/PDC, real ASIO/WASAPI audio, CPU and cold GUI tests must pass.
Do not mark upstream candidate PR merged as a factory release shipped.
