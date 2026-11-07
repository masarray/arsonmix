# FXF4 — Four first-party VST3 FactoryFX release path (2026-11-08)

## Product outcome
ArSonMix is **primarily a mixer with its native built-in FX**. Extra VST3
modules are optional bonuses, never a dependency for shipping or running it.
Bundle every separately approved and obtainable ArSonEQ, ArSonComp, ArSonVerb
or ArSonKuPik VST3. Zero to four extras is allowed; aim for at least one.
The native Parametric EQ already compiled into ArSonMix is a separate built-in
insert and remains available irrespective of external VST3 packaging.

This milestone completes the *four-product release intake, transactional stage,
production-four schema and verified cold-scan metadata cache*. It does **not**
misrepresent an empty approved lock as shipped binaries or claim physical FAT.

## Actual upstream binary authorities
| Product | Exact current public Windows VST3 candidate | Review status |
| --- | --- | --- |
| EQ | masarray/arson-eq, v0.1.0-m3.1-sc220-test | Prerelease/test; newer CI artifact is NOT a GitHub release |
| Comp | masarray/arson-comp, v0.2.0-sc220-vst3-test | Prerelease/test; stable v0.2.0 has Setup EXE, not VST3 ZIP |
| Verb | masarray/arsonverb, v1.0.1 | Published stable, AGPL-3.0-only |
| KuPik | masarray/vst-enhancer, v0.5.26 (DSP source: masarray/askp-vst) | Proprietary EULA/activation; redistribution and factory entitlement need recorded approval |

Release metadata watch includes *prereleases* solely as candidates. It excludes
drafts, ambiguous multi-VST3 ZIP listings, unrelated Chrome extension source
and standalone EXE artifacts. It does not automatically change
config/factoryfx-approved-releases.json.

## Exact release promotion and safety
1. Download each VST3 ZIP **on CI staging**, not on every user launch. Verify
   exact owner-reviewed SHA256, immutable source provenance, redistributable
   notices and plugin licensing; never copy private source/activation keys.
2. Run Windows JUCE VST3 host qualification for all four exact binaries: actual
   class FUID, plugin description uniqueId, mono/stereo/aux instance, wet DSP,
   bypass, multi-instance, states, latency/PDC and GUI lifecycle.
   A CI build, an upstream SHA file, or a filename is not qualification.
3. Add one reviewed lock record per **qualified** artifact with
   sourceCommit, released ZIP digest, VST3 class ID, actual host UID,
   licenseReview=approved, hostQualification=passed and entitlementPolicy.
   Include complete applicable notices in config/factoryfx-notices/.
   Never invent a SHA, FUID, host UID or approval flag.
4. Normal mixer packaging uses:
   python scripts/factoryfx_release_bundle.py stage --output dist --best-effort
   Each approved module is staged independently. Missing ZIP, invalid archive,
   checksum mismatch or absent notice skips ONLY that bonus, with a separate
   FACTORYFX_STAGE_REPORT.json recording included/omitted modules. The native
   mixer and its built-in EQ are still shipped, even if none is available.
   Separate strict qualification may use `--require four` without best-effort.
   Never advertise four shipped VST3 unless all four are in the verified ZIP.
5. Packager first materializes the entire approved package in a temporary
   same-volume directory. ZIP resource, manifest and notice checks run before
   one atomic FactoryFX directory rename. Failed staging leaves no half-published
   bundle, and the installer/portable ZIP consume the same verified content.
6. On first app launch, Registry rehashes bundled files and validates the
   approved manifest **on the scanner worker**. A separately cached plugin
   description may skip slow vendor VST3 metadata enumeration only when path,
   approved ZIP SHA, native JUCE host UID and product ID match the freshly
   validated package. No Factory cache enters the visible catalog until verified.
   Audio callback never hashes/scans/opens files/constructs plugins.
7. Runtime quarantines a corrupt/missing/UID-mismatched optional effect only;
   healthy approved FX remain available. The native EQ and mixer must stay live.
8. Show files keep plugin logical identity and state. Each FX must handle
   missing/invalid version explicitly; never substitute another vendor plugin
   or silently disable the whole audio engine.

## Release gates
- Fast Python offline tests: 4 supplier discovery, standalone-only stable
  skipped, prerelease selected, malicious/duplicate ZIP exclusion, exact-four
  identity, failed-second-download atomicity, tampered manifest rejection.
- Node contract tests: Factory-vs-user Vault, cached factory source remains
  invisible before independent Registry verification.
- Windows C++ native Registry negative cases, real VST3 load and DSP tests.
- Exact-head installer and portable archive: only successfully attested .vst3 bundles,
  their licenses, per-file hashes, accurate omission report, offline fresh install, existing mixer upgrade
  with safe shutdown, old Show recall, app/user separation.
- ASIO real monitoring with multiple instances, BUS/MAIN wet sends, 44.1/48/96k,
  128/256 frames where supported, rack power/bypass, CPU/xrun observation and
  audio-interface FAT. **Physical audio FAT pending until performed**.

## Cross-PR ownership
This implementation is based on current main. It supersedes stale
Comp-only watcher work in #225 without copying its old engine/GUI baseline.
Do not merge that historical patch on top blindly. Keep installer #443 and
device-retention #445 separate until each passes its own exact-head tests;
reconcile their eventual merged changes before shipping a final four-FX installer.
