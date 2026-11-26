# ZBSH-02 — Third-party and distribution rights inventory

Date: 2026-11-26. Audited main: 68707000d362c3096b7509fdde311b0be8fabbbb. Source review only; not a legal certification, a complete SBOM, or verification of previously shipped binaries. The exact release manifest and applicable license texts must follow the actual build.

## Reviewed components

| Component | Evidence and applicable terms | Status / distribution action |
|---|---|---|
| ArSonMix original software | [LICENSE](../../LICENSE), [LICENSE-NOTICE.txt](../../LICENSE-NOTICE.txt), existing SPDX headers | AGPL-3.0-only; preserve corresponding source, notices and modification history. |
| JUCE 8.0.15 | [exact upstream LICENSE.md](https://github.com/juce-framework/JUCE/blob/91ad83ae34a81e0833b1a2b0866f54846370ae53/LICENSE.md), alpha publisher checkout | AGPLv3 option for modules, not paid commercial EULA; include precise framework source and ArSonMix build patches in corresponding source offer. Verify actual release revision. |
| Steinberg VST3 SDK in JUCE | [exact SDK license](https://github.com/juce-framework/JUCE/blob/91ad83ae34a81e0833b1a2b0866f54846370ae53/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt) | MIT (2025). Full upstream license added to existing embedded notice; retain any additional component-specific notices. |
| Steinberg ASIO SDK in JUCE | [exact SDK license](https://github.com/juce-framework/JUCE/blob/91ad83ae34a81e0833b1a2b0866f54846370ae53/modules/juce_audio_devices/native/asio/LICENSE.txt) | GPLv3 alternative to proprietary terms, selected for AGPL project; GPLv3/AGPLv3 combination under section 13, not relicensing GPL portions. Full SDK notice and GPLv3 text added to embedded notice. Check individual SDK file terms. |
| Signalsmith Stretch 1.3.2 / Linear | [vendored inventory](../../third_party/signalsmith/README.md), upstream pins and MIT texts | Retain both MIT notices and exact source versions. |
| MIT KEMAR/HRIR | [SonKu provenance and attribution](../THIRD_PARTY_8D_SONKU_HRTF.md) | Retain MIT attribution and original measurement credit; research-only bank not automatically approved for distribution. |
| Inter | [OFL](../../web/assets/fonts/OFL-LICENSE-Inter.txt) | Retain SIL OFL and copyright; verify actual embedded font corresponds to notice. |
| VB-CABLE standard | [package lock](../../config/vbcable-official-package.json), [vendor notice](../../installer/VB-CABLE-NOTICE.txt), [vendor terms](https://vb-audio.com/Services/licensing.htm) | Vendor permits standard package redistribution as-is. Donationware, professional/organizational terms apply. Optional driver, no A+B/C+D, preserve vendor package, consent and license. No claim that professional use is always free. |
| FactoryFX optional VST3 | [approved release lock](../../config/factoryfx-approved-releases.json) | Current snapshot contains zero approved plugins. Do not infer rights from availability or license of host; verify each future binary/source/license and attribution before approval. |
| Microsoft WebView2 SDK/runtime | [prerequisite helper](../../installer/ArSonMixWebView2Prerequisite.ps1), CMake | Separate Microsoft terms; exact SDK version/license and included static loader notices not yet exhaustively established. Preserve official source and signed prerequisite; inventory next build. |
| Build tools/CI | CMake, Inno Setup, GitHub Actions, platform toolchains | Build-only vs shipped dependencies must be distinguished. Record actual versions and applicable redistribution obligations for included runtime files at ZBSH-04. |

## Artwork, media and asset provenance

| Group | Evidence | Remaining verification |
|---|---|---|
| App brand and icons | [approved master, hash and derivatives](../../assets/icon/README.md) | Owner-supplied; record original creator/rights evidence privately if needed. Do not infer registered trademark or copyright assignment. |
| Silver/gold fader artwork | [approved fader hash record](../ARI_FADER_ASSET_20261026.md) | Owner-approved assets; original license or creation record still to confirm where not documented. |
| Guitar FX procedural artwork | [design source](../../design-references/guitar-fx/README.md), [layer contract](../../design-references/guitar-fx/PRODUCTION_LAYERED_ART_CONTRACT.md) | Generator provenance documented; artist-source overrides and historical concept art require independent origin/rights evidence before shipping. |
| Other artwork | assets/fx, assets/imersip, web/assets, design-references, site/media | Per-file rights not independently established. Screenshots should use approved UI and no unlicensed third-party media; verify actual packaged subset. |
| External image, AI-generated image, font, sample/IR, model | New contribution or imported file | Record exact source/creator, license/terms, attribution and permitted redistribution. AI generation alone is not proof of rights; never use a vendor logo to imply endorsement. |

For unknown or restricted rights, do not approve the asset for a new distribution until evidence is obtained or an original/permitted replacement is used. Existing releases were not recalled or independently certified here.

## Release checklist (maintainer review, not a new CI gate)

- Identify exact source commit and actual shipped files. Preserve the complete corresponding source, build scripts, patches and needed upstream source references/copies under the relevant licenses; a moving branch URL is insufficient on its own.
- Compare actual SDK and component versions against this inventory; carry exact upstream copyright/license text, including additional file-specific notices. Preserve existing offline About & Licenses and byte-level packaging verifier.
- Verify new imported assets and third-party binaries separately; a hash identifies bytes but grants no rights. Record unresolved components and do not claim a clean license audit while open.
- Verify official source/release links and accurate release status; no publisher signature or registration claim unless actually proven.
- Run existing release/license fast contracts and exact-binary verifier at the next native packaging event. Do not initiate a redundant expensive native build merely for public policy text.
- Maintain this inventory as a source baseline; expand to a machine-readable exact-version SBOM at ZBSH-04/05.

## Exit / remaining risk

ZBSH-02 delivers public identity, copyright and vulnerability reporting policies, initial license/asset inventory, full VST3 and ASIO SDK notices plus GPLv3 in the existing legal resource. Private vulnerability reporting setting, actual binary notices and unresolved asset/SDK rights need separate verification. No paid signing, DJKI, proprietary license purchase, DRM, runtime changes or new GUI. The next security milestone ZBSH-03 owns repository settings and private-reporting activation.
