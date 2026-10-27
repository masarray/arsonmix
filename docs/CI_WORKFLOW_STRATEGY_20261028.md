# ArSonMix CI — efficient build / test / artifact contract (2026-10-28)

## One change, one appropriate validation lane

| Change | Pull request | After merging to main | User artifact |
|---|---|---|---|
| C++ `src/**`, `engine/**`, CMake, native tests | Windows MSVC Release + CTest + verified ZIP contents; UI only if UI paths also change | One Windows native run | One verified Windows x64 FAT artifact |
| GUI-only `web/**` or standalone | Chromium browser tests, GUI visual/interaction checks. No unnecessary Windows MSVC build | Chromium + one Windows packaged release, concurrently; Pages follows successful main Chromium | Latest main Windows artifact |
| Documentation-only | No costly C++/Chromium runner automatically | None unless a production input was changed | None (last qualified binary remains valid) |
| Hardware FAT on an unmerged branch | Select **Run workflow** for Windows Native VST3 Host on that branch | Not needed | Explicit branch Windows artifact |

Windows is the **single** producer of the distributable executable and must always run CTest, package and the independent ZIP/embedded GUI/provenance audit before upload. Pull requests deliberately do not upload a binary by default. The user or maintainer can request a branch FAT package through `workflow_dispatch`. Do not add independent Windows packaging workflows, publish an arbitrary test EXE, or ask for new hardware captures after every tiny edit.

## Queue and browser rules

- GitHub Actions concurrency cancels superseded in-progress Windows/Chromium PR attempts and old main revisions. A new commit or force push becomes the one revision to validate.
- UI PR checks run Chromium only. Firefox/compositor diagnostics remain on `main` (or explicit manual dispatch) as a heavier performance gate. Do not install both browser engines for every GUI iteration.
- Pages is **one** existing publisher, triggered only after successful **push-to-main** UI smoke. It checks out exactly the qualified head SHA and skips publishing when that SHA has been superseded by newer main. It will never deploy a PR or canceled/stale check. No parallel publisher and no redundant Pages deploy for native-only or docs-only changes.
- Screenshots retain only 3 days; Windows release FAT artifact retention remains 14 days. Raise retention only when there is an explicit long-running investigation.
- Trigger filters must include all code paths that affect the relevant shipped output. When introducing a new Windows build input, update the Windows paths list; when introducing a GUI input, update UI paths. Do not make the filters so narrow that a release could omit a change.
- If a required branch protection status requires both Windows and UI on all PRs, update required-check rules together with these path gates. Skipped workflows can otherwise be reported as pending by legacy branch protection. This GitHub connection cannot change rulesets automatically.

## Safety and transparency

A passing Windows job proves compilation, automated numerical/contract tests and release artifact integrity, **not** zero-crackle hardware audio or vendor sound positioning. A passing Chromium job proves UI interaction, **not** a native JUCE/WebView2 boot on arbitrary Windows devices. Audio callback performance and P0 issue #15 still need physical evidence.


## Reconciliation to current main (2026-10-28)

The CI change was rebuilt from main `c0d7c794a27fcdc7adbe77c6166a2e97b848b82c` rather than merging an older Windows workflow file. The existing Windows Release target list is preserved, including `ARMixFxBypassEnvelopeTests` and `ARMixDearVrPolarPolicyTests`. Only CI triggers, artifact gating, UI browser cadence and Pages freshness are changed. `tests/ci_workflow_efficiency_test.mjs` fails if mandatory audio regressions disappear from the future Windows target list.

The stacked routing PRs #108 → #110 → #111 remain **unmerged** and use the existing Windows workflow; when their CTest targets are integrated, do not replace their updated targets with a stale fixed list. PR #102 drag/reorder is independent and currently conflicting. Zero-plugin crackles #15 still need physical evidence, outside CI optimization.

Pages publishing has an explicit freshness check. A successful PR browser smoke may not publish to Pages: only successful UI smoke from `main` pushes qualifies. UI and Windows checks can execute concurrently on main, but only Windows creates the downloadable native ZIP. Branch-specific hardware builds remain an explicit manual request.
