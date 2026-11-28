# Standard FactoryFX four — owner direction 2026-11-27

The product target is four real bundled sibling VST3s in the same installer and portable. Supersedes the optional-only product goal for Standard, not the safety gate. An engineering FIELD can remain explicitly core-only before approval. Once all four are approved, even existing --best-effort workflows fail closed instead of silently omitting a locked module. No DSP, GUI, activation or render graph changes.

Exact candidates are in config/factoryfx-qualification-candidates.json: GitHub release asset digest and length, tag commit, and source commit claim. These are metadata, not downloaded bytes or host approval. EQ/Comp are prerelease; KuPik's published source is private and existing separate evaluation/activation must remain intact. No proprietary source or key is copied. EQ prior provenance mismatch (issue #185) requires checking the actual new ZIP.

## Evidence intake
From an isolated Windows runner with existing pinned JUCE configuration and BUILD_TESTING=ON:
```
cmake --build build --config Release --target ARMixFactoryFxHostProbe
python scripts/factoryfx_candidate_probe.py --output factoryfx-candidates.json --host build/ARMixFactoryFxHostProbe_artefacts/Release/ARMixFactoryFxHostProbe.exe
```
Omit --host for a cheap download/hash/archive/PE/moduleinfo inspection. A dedicated read-only Linux CI job runs this archive inspection when the pins/intake change; it never executes Windows binaries, creates an approved lock, or triggers a Windows build. Tool never writes approved lock, never stages candidates in dist, and leaves evidence only. Host probe is one process per candidate with 90s timeout, performs basic two-instance stereo finite PCM, state transfer and editor creation. It does not establish class ID from actual VST3 factory (moduleinfo only), wet DSP vs bypass, parameter equivalence, mono/aux adaptation, full editor lifecycle, licensing, sample-rate/PDC or physical FAT. Do not set hostQualification=passed from this smoke alone. Record corresponding source, complete notices, actual class ID and JUCE UID; resolve #60 entitlement without modifying activation. Promote only independently qualified records into the reviewed lock.

Existing exact-SHA standard staging and verifier enforce all locked modules (including all four). Normal core-only engineering packages remain possible before approval, but never label them Standard. The installer now independently verifies installed FactoryFX manifest/resources after fresh install and in-place upgrade using the same reviewed lock and portable receipt. Tests check downgrade rejection. Existing Installer/portable production lanes are retained; no extra Windows lane on every PR push.

Status at authoring: pinned metadata only; no download or Windows probe result yet, approved lock unchanged. No release approval or hardware claim.

## Release qualification extension — 28 Nov
Owner accepted FIELD source 0c831e193d936bca4c8234294d4e64dec24a9aa8, which did not contain FactoryFX. Exact first real Windows JUCE smoke at ab8a57b02c197628642d45bfa146447dd256c35d passed all four (run 46085667012, job 140794519096), including two instances, finite PCM and state/editor smoke. Extend the isolated test with runtime factory CID (not just moduleinfo), production stereo layout, automation/state value recall, bypass, 44.1/48/96 kHz × 64/128/256 frames with stereo and duplicated mono inputs and editor recreation. Results remain evidence, not a self-approved lock or a physical full-mixer test. New bundled modules need attended physical integration acceptance; do not treat the earlier core-only FIELD approval as their test. Preserve original KuPik separate EULA/activation.
