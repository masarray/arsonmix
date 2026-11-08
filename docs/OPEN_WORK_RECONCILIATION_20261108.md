# ArSonMix — Open PR / Issue reconciliation (2026-11-08)

## Verified repository checkpoint

- Repository: [masarray/arsonmix](https://github.com/masarray/arsonmix)
- Main audited at: `c57720ea25585c861af89802e1365ffef7d03d17`
- Open PRs before sweep: **13**; after sweep: **0**.
- Open standalone issues before sweep: **19**; after sweep: **13**.
- No old GUI, PCM callback, M4 admission path or temporary CI override was bulk-merged during cleanup.
- Current code authority is merged [#418](https://github.com/masarray/arsonmix/pull/418) plus subsequent merged DAW/editor/release changes. ONE assembled approved `/preview/` feeds public Pages and native JUCE, with only one realtime PCM callback.
- This is an *administrative reconciliation*; no Windows, macOS, Linux, hardware FAT, VST3 or actual PCM CI is inferred from issue closure.

## Closed open PRs — archived (NOT merged)

| Reason | Historical PRs |
| --- | --- |
| Pre-#418 competing DAW hardware input / M4 iterations | [#413](https://github.com/masarray/arsonmix/pull/413), [#415](https://github.com/masarray/arsonmix/pull/415), [#416](https://github.com/masarray/arsonmix/pull/416), [#417](https://github.com/masarray/arsonmix/pull/417), [#409](https://github.com/masarray/arsonmix/pull/409) |
| Integrated features already replaced by current source | [#421](https://github.com/masarray/arsonmix/pull/421) FX rack, [#434](https://github.com/masarray/arsonmix/pull/434) channel appearance |
| Old stacked P1 / standalone PEQ experiments unsuitable for direct merge | [#422](https://github.com/masarray/arsonmix/pull/422), [#423](https://github.com/masarray/arsonmix/pull/423) |
| One-off CI exception, field A/B, retired FAT and 106-file GUI-source migration | [#436](https://github.com/masarray/arsonmix/pull/436), [#251](https://github.com/masarray/arsonmix/pull/251), [#270](https://github.com/masarray/arsonmix/pull/270), [#393](https://github.com/masarray/arsonmix/pull/393) |

Each PR body has a specific reconciliation rationale. Git branches and commits are **retained**; closing the PR does not authorize silently copying old DSP into `main`.

## Closed standalone issues — explicitly resolved/superseded

- Old team ownership / mandatory collision gates: [#397](https://github.com/masarray/arsonmix/issues/397) `not_planned`.
- Separate historical V6 renderer ownership: [#268](https://github.com/masarray/arsonmix/issues/268) `not_planned`.
- Historical multi-agent GUI execution ledger: [#351](https://github.com/masarray/arsonmix/issues/351) `not_planned`.
- Current approved GUI implementation-level scope: [#346](https://github.com/masarray/arsonmix/issues/346) direct WIRE gesture, [#348](https://github.com/masarray/arsonmix/issues/348) operator chrome, [#361](https://github.com/masarray/arsonmix/issues/361) FX/rail `completed`.
- GUI source implementation milestones are not physical audio listening certificates. All remaining real engine and usability bugs still have active tracking.

## Exactly 13 intentionally open product issues

| Focus | Live issue | Concrete next acceptance |
| --- | --- | --- |
| Critical audio operation | [#412](https://github.com/masarray/arsonmix/issues/412) | Any hardware IN → channel FX/WIRE → MAIN, SOURCE ONLY / BUS / reconnect, automatic beginner flow, correct native ACK/PCM |
| Dynamic mixer | [#192](https://github.com/masarray/arsonmix/issues/192) | Add/remove/reorder arbitrarily sized channels with stable IDs, real FX/routing/Show persistence |
| One GUI/native parity | [#389](https://github.com/masarray/arsonmix/issues/389) | Same approved renderer on Pages and installed native, true graph controls/readback, parity and no second GUI |
| Capture reliability | [#15](https://github.com/masarray/arsonmix/issues/15) | Evidence-backed zero-FX crackle / discontinuity and hardware diagnostics |
| Factory FX | [#185](https://github.com/masarray/arsonmix/issues/185) | Verify first-party VST3 identities, manifests, runtime loading and OS packaging before shipping |
| Browser/GPU feel | [#56](https://github.com/masarray/arsonmix/issues/56) | Measure idle-pointer stutter on affected Windows hardware |
| Fine pointer controls | [#347](https://github.com/masarray/arsonmix/issues/347) | One predictable pointer/hover-wheel model, no stutter or unintended changes |
| Cable appearance | [#349](https://github.com/masarray/arsonmix/issues/349) | User-accepted wire gravity/damping in actual GUI |
| User-owned plugin vault | [#85](https://github.com/masarray/arsonmix/issues/85) | One unrestricted app, persistent portable VST state without rescans |
| Show/Scene safety | [#180](https://github.com/masarray/arsonmix/issues/180) | Safe scoped recall and complete FX/vendor-state recovery |
| Native loudness | [#135](https://github.com/masarray/arsonmix/issues/135) | Actual BS.1770 LUFS-I and ≥4x measured dBTP, never demo readings |
| OS distribution | [#453](https://github.com/masarray/arsonmix/issues/453) | Verify all three OS cards on primary Cloudflare host use actual official packages |
| **Release command gateway — KEEP OPEN** | [#450](https://github.com/masarray/arsonmix/issues/450) | Owner-authorized exact-SHA release/port/publish comments; avoid duplicate expensive runs |

The owner should not need to restart obsolete reviews, coordinate separate GUI/ENGINE teams, or manually launch duplicate Windows/Chromium jobs. Focus next native engineering PR on #412 and #192 before lower-impact visual refinements.

## Public Windows / Linux / macOS release evidence

Same immutable source `d7cf109ad1d45f0666431ce748d72572dd49facc` and tag `v0.5.0-alpha.5`:
- Windows: real `Windows-x64-Setup.zip`, Setup EXE and portable ZIP.
- Linux: actual x86_64 ELF `.tar.gz` after [release/CTest PASS #42446499625](https://github.com/masarray/arsonmix/actions/runs/42446499625).
- macOS: native universal arm64+x86_64 `.dmg` after [release/CTest PASS #42446684702](https://github.com/masarray/arsonmix/actions/runs/42446684702). macOS ad-hoc signed, not Developer ID notarized.
- Official [GitHub Release](https://github.com/masarray/arsonmix/releases/tag/v0.5.0-alpha.5) contains three OS assets and platform-specific checksum/provenance records.
- [GitHub Pages release catalog](https://masarray.github.io/arsonmix/release-catalog.json) contained all three OS files at audit.
- [Primary Cloudflare Pages release catalog](https://arsonmix.pages.dev/release-catalog.json) **lagged** with Windows-only at audit. Its deployed JS has a read-only GitHub Release freshness fallback, but the *static* catalog still needs a fresh Git-connected Pages deployment. The substantive docs-only reconciliation commit on main is expected to cause one Cloudflare Pages build with the updated catalog. Verify live before closing #453.

No Windows MSVC/Chromium/manual port CI was dispatched for this cleanup. Field/device testing is not required for users to begin trying the published experimental builds, but native stability claims remain separate from CI proof.
