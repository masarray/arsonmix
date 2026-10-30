# ADR FXF1 — ArSonMix Factory FX: six first-party VST3, one native host
Date: 2026-10-30 | Status: ARCHITECTURE APPROVED, RUNTIME FACTORY BUNDLE NOT DELIVERED
Source audited: ArSonMix main@aa9b9c810353ed78c3df941213c939432d486d26. Product epic: https://github.com/masarray/arsonmix/issues/185. Recheck latest main before implementation; MS1 PR #184 and G5A PR #178 are MERGED, not to be recreated.

## Binding decision
Official ArSonMix installer AND portable ZIP must eventually include six truly functional first-party effects: **ArSonEQ, ArSonComp, ArSonVerb/Reverb, ArSonHarmony, ArSonColor and ArSonKuPik Enhancer**. Any user may also import their own VST3 and use identical application features. The app contains no owner-specific/private user VSTs, presets or credentials.

Integrate the six as complete **separately built VST3 modules**, loaded by ONE existing ArSonMix native JUCE host/FX graph, not as copied source, a new internal Web Audio renderer, one JUCE runtime linked into another, or fake FX toolbar buttons. VST3 binaries may be built with different JUCE revisions; same-ABI host compatibility must be verified empirically. Do not ship standalone EXEs inside the Factory bundle.

## 1. Sibling-source audit / exact build ownership
- **ArSonKuPik**: source private masarray/askp-vst master@d09fc11, product v0.5.26, JUCE8, manufacturer code MsAr, plugin code AkVs. Public Windows-x64-VST3 ZIP and SHA256SUMS on https://github.com/masarray/vst-enhancer/releases/tag/v0.5.26 . vst-enhancer is release/site distribution, NOT DSP source. Proprietary EULA/activation/redistribution and current JUCE tier must be approved before ArSonMix public inclusion. Never silently disable the licence mechanism. Owner issue https://github.com/masarray/askp-vst/issues/60
- **ArSonEQ**: masarray/arson-eq on feature branch feat/initial-vst3-eq-architecture@1f9513d contains DSP/editor and JUCE9 VST3+Standalone CMake target (ArsN / ArEQ), AGPL source licence. Default main@d9e2b22 contains only LICENSE, and there is no public GitHub Release at audit. Do not pull main expecting an EQ plugin; require pinned candidate artifact, host qualification, then branch/release promotion. Owner issue https://github.com/masarray/arson-eq/issues/6
- **ArSonVerb**: masarray/arsonverb main@69fe83c has reusable C++ DSP and opt-in JUCE9 VST3 (Arst / Arvb), BUT LICENSE-STATUS.md says outbound licence undecided and there is no release. Public bundle blocked until owner chooses licence and completes host validation. Owner issue https://github.com/masarray/arsonverb/issues/4
- **ArSonComp**, **ArSonHarmony**, **ArSonColor**: dedicated owned sibling source repositories were not found at audit; require real independently testable DSP + VST3 wrappers, no invented placeholder effects. Compressor requires attack/release/knee/ratio/makeup and measured gain reduction; Harmony requires musical pitch/formant tracking and declared latency; Color requires musical saturation/tone, measured alias/oversampling and headroom.

## 2. Distribution layout: immutable Factory vs user-owned Vault
Proposed portable layout:
    ArSonMix/
      AR MIX Studio.exe
      portable_mode.txt
      FactoryFX/
        factoryfx-manifest.json        # generated only from actually shipped/validated binaries
        ArSonEQ/ArSonEQ.vst3            # complete Windows bundle once signed off
        ArSonKuPik/ArSonKuPik.vst3     # after entitlement approval
        ArSonVerb/ArSonVerb.vst3       # after legal/host signoff
        licenses/                     # all per-product notices/EULA
      Studio/
        Plugins/VST3/                  # user-owned; empty on public release
        Shows/Presets/                 # private user data; empty on public release

Installed EXE uses its installation directory for read-only/atomic-update FactoryFX, and existing installed/portable PortableWorkspace data root for USER plugins. FactoryFX is NEVER the destination for manual user imports. Factory binaries are NOT committed to Git nor silently downloaded by every app startup.

## 3. Versioned manifest / Factory Plugin Registry
Add a lightweight Factory registry into existing src/PluginCatalog.h and existing native plugin state pipeline; do not implement a separate scan or DSP graph.
Each actually SHIPPED manifest entry carries:
- stable productId, name, FX category EQ/COMP/REVERB/HARMONY/COLOR/ENHANCER
- pinned plugin version, source commit/tag, approved distribution/legal notice version
- verified Windows x64 VST3 relative bundle path; complete archive digest and per-file SHA256
- actual discovered VST3 CLASS ID and supported I/O layout/sidechain, not an invented class ID derived from manufacturer/plugin-code text
- host-compatibility qualification, selected licence/activation status, available presets and migration metadata.

Startup resolves only manifest-listed Factory modules, off RT; bounded fingerprint check and durable PluginDescription cache keyed by approved artifact digest+class identity. No repeated global directory scan or all-vendor plugin loading every launch. Missing/modified DLL, wrong class ID, mismatched dependency or inaccessible path is a visible failure, not a fallback to an unrelated plugin with the same name. Only actual installed/validated entries can appear usable in Factory picker. Internal logical Show references use factory://productId with version+plugin class identity and vendor binary state; USER workspace:// paths keep independent ownership and existing compatibility rules.

## 4. Packaging and licence gates
A separate controlled packaging pipeline (NOT a public CI clone of private source) obtains immutable approved sibling release artifacts, checks official SHA256 provenance, extracts full VST3 bundle with path traversal/symlink protection, checks x64 binary resources/class ID and per-file hashes, includes all required notices and then atomically stages one complete FactoryFX folder. Official release MUST FAIL if a required plugin is unavailable or its hash/legal approval is missing; an engineering partial bundle must be explicitly named as partial, never called six-effect production bundle. Do not copy user Vault or private activation/signing keys into ZIP.

ArSonMix host is AGPL; ArSonEQ is AGPL; ASKP-VST has separate proprietary terms and licence manager; ArSonVerb outbound licence is TBD. Being the product owner and distributing VST3 adjacent to an AGPL host does NOT itself settle distribution compatibility. Record an explicit reviewed owner decision for each package/JUCE8-or-9 dependency, third-party source, source notices, redistribution and FX entitlement. ArSonKuPik remains activated/evaluation according to valid licence terms unless the owner formally approves a new included entitlement; no bypass by host. The UI distinguishes installed, active/licensed, inactive and incompatible.

## 5. One native host, not new engine or plugin bypass
Reuse current native G5A (#178) prepared multislots, existing CH1 FX/Orbit and evolving G5B #95 user-facing multi-insert. Mono/stereo source/channel from merged MS1 #184 must be respected; plugin bus layouts and latency/PDC on control thread, plus PRE/POST FX AUX from MS2 #174. No VST instantiation, hashing, license networking, file scanning or state serialization on realtime audio callback. Use existing graph revision, MUTE, native ACK, positive/negative rollback and Safe Show state (#180). Vendor VST GUI opens lazily and does not multiply WebView2 renderers for idle mixer strips. L2 #176 all eleven fader tracks remain fixed and aligned.

## 6. Release milestones (complete vertical deliverables)
FXF1: qualify existing ASKPVST v0.5.26 and ArSonEQ feature branch for Windows, approve licensing, create verified Factory registry+cache, staging scripts, exact release manifest, Factory/My Plugins picker, one public/portable executable product that loads actual two original FX in CH1/CH2, handles vendor Show state and produces exact-head Windows ZIP with real VST3 assets. An EQ source branch or ASKPVST release URL is NOT a shipped factory plugin.
FXF2: certify and package ArSonVerb after licence decision; develop original ArSonComp, ArSonColor and ArSonHarmony from core DSP through independently testable VST3, and ship all SIX effects with meaningful presets and real response.
FXF3: atomic Factory FX upgrades/rollback, version-safe Show migrations, plugin failure handling and multi-channel/PDC profiling.

Acceptance: actual Windows x64 VST3 load and multiple instances, mono/stereo and sidechain compatibility, sample rate 44.1/48/96k and actual supported buffers, bypass/automation, state capture/restore, shared AUX reverb, latency/null and CPU measurement, WebView2 editor and native fallback where offered, test on ordinary integrated GPU Windows PC. Valid native graph ACK and Windows compile do NOT clear actual zero-FX hardware crackle blocker #15. Use targeted tests before one exact-head packaging CI. No full CI for docs-only change.
