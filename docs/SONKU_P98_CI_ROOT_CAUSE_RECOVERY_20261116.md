# P9.8 / PR #729 — actual red-CI root causes and controlled repair (2026-11-16)

## Operator checkpoint

The approved *Venue · Mega Space* P9.7 commit `fda0eef669e0a59575c489e149662c1bf878c998` remains immutable on `checkpoint/sonku-p97-venue-mega-space-approved-20261116`. No public release or main merge is authorized by this repair. No Venue curve/preset/PCM coefficients change.

## 1. True v12 Windows CTest failure

Exact-head workflow run [43828402066](https://github.com/masarray/arsonmix/actions/runs/43828402066) compiled Windows ARMixConsole and JUCE tests successfully, then failed **1/11** in `NativeSonKuP90JuceV11AppliedAckAndLegacyRestore`, test line 299. The negative fixture writes retired physical/mono `renderMode=2` to bytes 50..53 of a 67-byte v12 state. The v11 restoration code rejected this; v12 was accidentally omitted from the v11-only mode whitelist. With typed `sceneVoicing=0`, the generic SonKu core validator also permitted the operator-rejected legacy P8.3 modes. This is a genuine native state-acceptance defect, not an unrelated flaky Windows build.

**Repair:** P9.8-gated `SonKuSpatialCore::valid` requires mode 0 or 3..9, no legacy LF480, and selected voicing only for mode9. `SonKuSpatialProcessor::restoreUnpublishedState` checks the same whitelist for v11 *and* v12. Negative tests now cover modes 1 and 2 in native JSON, native PCM queue and corrupt v12 restore; also retain positive v11 restore and v12 round-trip, exact Venue/ROOM-OFF/MIX=0 hashing. Do not simply delete the failing assertion.

## 2. Retired P8.3 negative FAT caused unrelated PR to look red

[Run 43828402288](https://github.com/masarray/arsonmix/actions/runs/43828402288) failed with `P83_HEADPHONE_FAT_CANDIDATE ... REJECTED_LOW_ENERGY_OR_SPECTRAL_HOLE`. This intentionally negative research mode is neither the current accepted P9.7 Venue nor supported by P9.8. It is NOT repaired or made musically qualified. Its strict test is retained for dedicated `research/sonku-p83-*`/`feat/sonku-p83-*` branches and manual `workflow_dispatch`, instead of firing for every shared SonKu core edit.

## 3. Two broken 0-job GitHub workflow runs, not a DSP fault

[Run 43835166813](https://github.com/masarray/arsonmix/actions/runs/43835166813) reports `Invalid workflow file: .github/workflows/sonku-p89-windows-field.yml#L346`. The independent P90 workflow also produced an immediate 0-job push failure. Both files had a **pasted duplicate Windows test block after the first upload-artifact `with:` map**, including an executable PowerShell `if ($LASTEXITCODE ...)` line illegally indented inside YAML. A quoted `ctest -R` PowerShell expression was also unterminated. Both needed source repair.

**Repair:** Remove the duplicated tail and wrong YAML indentation. Close native CTest regular-expression quoting. Maintain exactly one Windows compile/CTest/portable artifact workflow each. P8.9 is genuinely v10 (without P9.0 CMake flag, no P9.0 tests, state schema 10, P8.9 docs/ZIP/provenance); P9.0 remains v11 (state schema 11). They are scoped to their own historical research branch, explicit manual dispatch, or P9.0 reusable workflow call. No duplicated heavy x64 build on the newer P9.8 PR.

`scripts/verify_sonku_legacy_workflows.py` runs in the existing P9 source preflight with only Python standard library and blocks reintroducing duplicated artifact steps, unterminated CTest commands, schema mismatch, or accidental P9.0 runtime in P8.9.

## Verification rule

Use **exact SHA** of this repair commit, not an older run, and require the full Windows P9.8 JUCE CTest plus exact-head portable provenance and Chromium smoke. Source/PCM spectral ratios do not prove subjective quality; the user still performs level-matched headphone FAT before promoting Signature or Cinema. Historical failed runs remain in GitHub history even if the newly corrected workflow validates successfully.
