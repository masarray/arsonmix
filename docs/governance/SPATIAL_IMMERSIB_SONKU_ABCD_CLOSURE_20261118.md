# Milestone A–D closeout — approved ImmerSIB and 8D-SonKu (18 November 2026)

**Scope:** THIS document closes the **spatial audio release/governance workstream only**, not the whole mixer Milestone A–D program, hardware FAT, MVVM, dual-ASIO, or future acoustic research. Immutable accepted ImmerSIB and 8D-SonKu P9.9 audio remain the reference; no unapproved retuning or wholesale research branch import.

## Owner-accepted spatial baseline

- **8D-SonKu P9.9 listening-approved:** selective production merge [PR #733](https://github.com/masarray/arsonmix/pull/733); preserve `checkpoint/sonku-p99-listening-approved-20261117` and the earlier owner-approved P9.7 **Venue Mega Space** reference `checkpoint/sonku-p97-venue-mega-space-approved-20261116` (SHA `fda0eef669e0a59575c489e149662c1bf878c998`). Do not flatten the desired 3D elliptical motion or accidentally make power-off speakers animate.
- **ImmerSIB native product:** selected [PR #734](https://github.com/masarray/arsonmix/pull/734), editor fix [#742](https://github.com/masarray/arsonmix/pull/742), operator acceptance [#746](https://github.com/masarray/arsonmix/pull/746). Catalog ID **`armix.builtin.imersip.v1`** and persisted **IMRS** identity remain stable. Display-name rebrand must not break `.smix` recall.
- **Acoustic source freeze:** the D5 package tests 29 accepted native processor/scene/DSP files against the alpha.26 source. Keep the existing editor, presets and bass/vocal/room timbre; any later acoustic revision requires an explicitly new owner listening qualification, not a silent VM/refactor side-effect.

## What A–D accomplished (spatial-relevant boundary)

| Milestone | Integrated evidence | Closure |
|---|---|---|
| **A: research branch reconciliation** | [PR #747](https://github.com/masarray/arsonmix/pull/747) merged; archival ledger of 38 superseded SonKu/ImmerSIB PRs preserves original SHAs and does NOT merge their unapproved audio | **COMPLETE** for research-PR hygiene |
| **B: issue/status audit** | [PR #749](https://github.com/masarray/arsonmix/pull/749) merged; spatial research and listening checkpoints distinguished from active release defects; [#726](https://github.com/masarray/arsonmix/issues/726) remains optional future research | **COMPLETE** for issue triage, not future research |
| **C: real public downloads** | [PR #750](https://github.com/masarray/arsonmix/pull/750) and repair [#751](https://github.com/masarray/arsonmix/pull/751) merged; verified release catalog, server-rendered download links, offline/fail-closed Pages behavior | **COMPLETE** for spatial product distribution path |
| **D4: merge policy** | [PR #754](https://github.com/masarray/arsonmix/pull/754) and [#758](https://github.com/masarray/arsonmix/pull/758) merged; required main status `ArSonMix / Required CI Merge Gate`, previously tested with blocked red PR and allowed green docs PR | **COMPLETE** for guarded D5 integration |
| **D5: compiled spatial product & release** | Corrective [#759](https://github.com/masarray/arsonmix/pull/759), final [#760](https://github.com/masarray/arsonmix/pull/760) merged into `main` as `42841dc9b451d84a3ee0332008b3af0e21ff51d2`; no DSP/preset retune | **COMPLETE** for alpha release and main reconciliation |

## Acceptance and real build evidence

1. Rejected incomplete D5 FAT [#44081695177](https://github.com/masarray/arsonmix/actions/runs/44081695177) built an executable without ImmerSIB's native C++ catalog; **never** use that test installer.
2. Corrected Windows FAT [#44175141112](https://github.com/masarray/arsonmix/actions/runs/44175141112) **SUCCESS**: **102/102 CTests**, native ImmerSIB JUCE preset/editor/state tests, P9.9 Venue golden, six embedded scene images, 29 accepted source blobs, installation/upgrade/rollback and artifact integrity. Cross-platform absolute sample hashes are not claimed bit-identical under MSVC versus GNU.
3. After reconciling concurrent main governance/documentation work, the exact-head [Windows native #44186004915](https://github.com/masarray/arsonmix/actions/runs/44186004915) **SUCCESS** and native source changes passed protected merge on [#760](https://github.com/masarray/arsonmix/pull/760), with no override of branch protection.
4. **Official public Windows alpha:** [v0.5.0-alpha.27](https://github.com/masarray/arsonmix/releases/tag/v0.5.0-alpha.27), source `a93759d6843e30f75a4a28adf9a09c0c1c375fa3`, release publisher [#44185766988](https://github.com/masarray/arsonmix/actions/runs/44185766988) **SUCCESS**, seven uploaded assets (verified Setup/Portable/checksums/provenance), Pages deploy [#44186717276](https://github.com/masarray/arsonmix/actions/runs/44186717276) **SUCCESS**. Release branch adds notes to reconciled D5 source; it does not replace main or change acoustic DSP.
5. **Owner physical listening acceptance:** installed Windows candidate **audio plays successfully** and owner authorized public publication. This does **not** prove all ASIO/WASAPI interfaces, 10-channel routing, third-party VST3, hardware dropout margin, or genuine old-user auto-update.

## Safe boundaries for the next MVVM thread

- This spatial A–D workstream is **CLOSED FOR ALPHA.27**. Do not repeat its archived P7–P9 research, create another D5 installer, or retune Venue/Signature/Cinema/bass/room just to refactor the GUI.
- MVVM should remain presentation-only in the single approved packaged `/preview/` GUI; native JUCE bridge, spatial C++ DSP, actual PCM owner, IMRS/`.smix` identities, native editor lifetime, release feature flags and six scene assets remain authoritative. Do not copy old `web/mixer-io.js` into the new GUI.
- **Deferred, independent tracks:** [#726](https://github.com/masarray/arsonmix/issues/726) further spatial scene geometry (P2; not alpha.27 blocker); [#412](https://github.com/masarray/arsonmix/issues/412) physical mixer routing FAT; [#625](https://github.com/masarray/arsonmix/issues/625) real alpha.26→alpha.27 auto-update FAT; [#547](https://github.com/masarray/arsonmix/issues/547) historical startup/VST3 incident requiring fresh reproduction. None is silently reclassified as a completed spatial A–D acceptance test.
- No multi-device code, general CI compiler optimization, Guitar FX visual research, mixer architecture rewrite or broad issue closure is part of this handoff.

**Operator action:** none required to close the A–D spatial thread. Continue the independent MVVM milestone from the latest `main`, inspecting its active PR head and preserving these immutable sonic and release references.
