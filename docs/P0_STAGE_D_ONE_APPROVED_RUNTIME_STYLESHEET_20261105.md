# P0 Stage D — one runtime CSSOM and qualification

**Issue:** https://github.com/masarray/arsonmix/issues/56
**One integration PR:** https://github.com/masarray/arsonmix/pull/414
**One real GUI:** the official /preview/ and its exact JUCE WebView2 counterpart.

## What actually changed

The deterministic source assembler still reads 18 authored CSS owners in
the same approved sequence and runs the conservative Stage C declaration
pruner, retaining WebView2 version-dependent fallbacks.

Stage D concatenates the already compiled CSS owners into ONE generated
content-addressed assets/arsonmix-approved-runtime.css, preserving their
exact selector, priority and at-rule order. Published HTML and JUCE WebView2
each request ONE CSS resource, versus 18 before: 17 fewer stylesheet
requests. No new authored override, alternate UI skin, CSS style node
or second renderer is created.

The original 18 stylesheet source owners and compiled per-owner copies
remain as inspectable provenance. Unreferenced CSS copies never become
loaded CSSOM layers. The standalone /preview/ and JUCE use the same
scripts/build_pages.py assembler.

Unsafe @import, @charset and @namespace in an owner are rejected instead
of silently changing their behavior under bundling. Image/font URLs
still resolve relative to the same assets directory. The approved
MUTE/SOLO/PFL hardware PNG and historical WIRE CSS remain byte-locked.

## Exact-GUI tests

- tests/single_preview_publication_contract_test.py: 18 ordered source
  owners, per-file compiled hashes, one generated bundle SHA, one runtime
  stylesheet link and approved PNG/WIRE assets.
- tests/approved_css_stage_c_compiler_test.py: cascade priority,
  scoped selectors, old WebView2 and missing-variable fallbacks,
  deterministic ordered bundle, unsafe-at-rule rejection.
- tests/approved_css_stage_d_single_bundle_smoke.py: Chromium three
  resolutions, exact one CSS request/CSSOM, PNG artwork and MUTE click.
- Stage C visual parity tests reconstruct the 18 original CSS links in a
  TEST FIXTURE only, then check exact pixels/computed style, hover and
  geometry against the one-bundle runtime.
- Historical V28 byte-exact source test, Stage A-B profiling,
  no-new-CSS-stacking gates, Windows native JUCE/CTest and actual
  First Sound/FX/WIRE control tests must all remain green.

## Honest limitations

**ONE loaded stylesheet does not mean ZERO duplicate declarations inside
the historic authored CSS.** Stage A+B counted 114 cross-owner and 252
within-owner repeated selector/property combinations. The compiler
safely removes cascade losers; required fallbacks and locked artwork may
retain old declarations. No-new-stacking CI gates still apply. A source
rewrite needs separate explicit checkpoint migration and visual proof,
never a silent regeneration of historical acceptance checks.

One fewer group of network requests is a structural improvement, not
proof that Windows GPU paint/input-to-photon p95/p99 improved. Keep issue
#56 open until measured on the affected Windows GPU. Neither headless CI
nor WebView2 compilation proves physical ASIO/WASAPI/VST3 FAT.

## Operator capture

Profiler default OFF. On the Windows EXE press Ctrl+Alt+Shift+F9 to
record 15 seconds, then record exact EXE SHA, GPU driver, DPI/refresh,
JS/native timing log and Edge/WebView2 paint trace. See Stage A
profiling operator instructions for the comparison matrix.
