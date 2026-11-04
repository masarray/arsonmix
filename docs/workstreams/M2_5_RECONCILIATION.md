# M2.5 — REPOSITORY / ARCHITECTURE RECONCILIATION

**Authoritative progress ledger:** https://github.com/masarray/arsonmix/issues/399  
**Baseline:** `main@c1a11afd145380a944c9844937ccb6a97b5e0557` (M2 #394 merged, one approved /preview/).  
**Purpose:** prevent concurrent ChatGPT threads from creating competing GUI, bridge, native PCM, or FAT implementations.

## Ownership (one authoring branch per runtime subsystem)

| Runtime scope | Sole active owner | Work retained | Conflict disposition |
|---|---|---|---|
| GUI visible DOM/CSS/PNG | Approved /preview/ source, GUI #393 | only existing visual, no redesign | #393 consolidation deferred until joined native stable; port without overwriting current bridge |
| GUI↔native accepted state | M2 #394 merged; #396 repairs original IIFE/gesture | one `approved-native-contract.js`, one `approved-native-bridge.js` | #395 alternate M2 controller redundant; preserve unique validations, retire duplicate producer |
| JUCE callback, PCM graph, VST3 lease | #266 only (WIRE #387 absorbed) | dynamic ChannelId/EdgeId PCM; CH1 10CH+4AUX compatibility | FAT #270 and WASAPI #251 must not bulk overwrite engine |
| Pre-release admission | #398 | canonical /preview/ & callback gates | no competing puzzle/release check |
| Windows UAT/FAT | #270 testing only + #15 physical rig | exact joined SHA Windows bundle/PCM/VST3/device measurements | no engine or GUI implementation owned by FAT |
| WASAPI reliability | #251 after #266 cutover | 0 FX dry path crackle evidence | do not parallel-edit `LiveAudioEngine.h` |
| FactoryFX sibling plugin packaging | #225 | signed/manifests/release watch | no engine callback or GUI replacement |

## How threads coordinate

1. **Before editing:** read `main`, `AGENTS.md`, issue #399, open PRs, and exact current HEAD. If a PR owns your files/domain, contribute there or add a handoff; **do not create another production path**.
2. **Every PR:** identify one owner, base SHA, changed production domains, native GUI parity, current CI, and the PR it supersedes.
3. **Merge order:** unique GUI scope correction #396 → integrate #266 on CURRENT main (with Main.cpp event handlers and M2 native asset producer retained) → #398 source/release gate → #270 FAT-only evidence. #393 byte-identical source cleanup can follow sound cutover. Order may change based on tests, never by bulk merge.
4. **At merge:** verify exact-head Windows + Chromium, graph/ACK/snapshot test, rejected/stale PCM behavior, no second callback/renderer. Hardware certification is independent. Close or explicitly mark superseded work after its unique tests are ported.
5. **CI efficiency:** `scripts/check_workstream_ownership.mjs` is a cheap step in existing workflows (no new runners). PR ≤ #399 gets actionable warnings; new PR ≥ #400 fails if it overlaps an OPEN owner's production domain. Workflow/CMake shared junctions are warnings requiring manual review, not an artificial blocker.

For live auditing on a PR checkout:
```sh
node scripts/check_workstream_ownership.mjs
node --test tests/workstream_ownership_guard.test.mjs
```

**Limits:** CI detects cross-PR workstream overlap, not actual concurrent unsaved edits in another ChatGPT session. Branch protection/rulesets require GitHub repository admin access; passing CI alone cannot enforce merging on an unprotected `main`.

**Definition of done:** current baseline, unambiguous active owners, no duplicate bridge owner, exact-head qualified joined engine+GUI, FAT still PENDING until hardware evidence. No GUI changes for M2.5.
