# P0 Stage C — deterministic CSS consolidation (2026-11-05)

**Scope:** exactly the official generated /preview/ GUI and its JUCE WebView2 native equivalent. Tracking [issue #56](https://github.com/masarray/arsonmix/issues/56), integrated [PR #414](https://github.com/masarray/arsonmix/pull/414).

## Architecture

Stage A/B added an opt-in profile and guarded the historic 114 cross-file plus 252 same-owner selector/property repetitions against further growth. Stage C implements genuine build-time pruning of **provably redundant runtime declarations**, via scripts/compile_approved_css.py integrated in the ONE official assembler scripts/build_pages.py. The source-owned CSS files and V28 byte-exact historical fixtures are NOT modified.

For the **same exact normalized selector**, **same property**, **same conditional at-rule chain** and the actual ordered CSS modules, retain only the last !important declaration if one exists, otherwise the last normal declaration. A declaration that can never be the cascade winner is removed from the COPIED output CSS. 18 existing CSS module links stay in their original order: NO new stylesheet, no stylesheet layer on top and no CSS specificity/selector/value rewrite. Build-manifest counters and content-addressed CSS URLs make this inspectable and cache-safe.

Excluded intentionally: different @media/@supports/@container/@scope/@layer contexts; nested or unrecognized rules; @font-face and @keyframes; and byte-locked approved PNG hardware-key CSS plus WIRE CSS source. This is a conservative compiler, not a false claim that unrelated semantic selectors or different responsive states can always be collapsed. Technical debt in source remains visible for subsequent source-owned consolidation with visual proof.

## Tests (must remain fail-closed)

- Compiler unit test covers normal versus !important priority, cross-file and same-file losers, conditional media/supports, URL/comment quote syntax, keyframes, original PNG/WIRE bytes, idempotence and cache hashing.
- Publication gate independently derives the exact compiled bytes of every CSS owner from immutable source, verifies every per-owner hash and original DOM, approved PNG hardware art, and only one /preview/ renderer.
- Browser visual parity test renders the ORIGINAL approved GUI at 1366×768 and 1600×900 with unoptimized source CSS and with Stage C compiled CSS, comparing computed properties, pseudo-elements and PNG screenshots, including pointer hover. Only the absolute temporary file:// asset root is normalized; actual asset name, URL query, style and visual output must remain equal.
- Prior immutable V28 CSS recovery, full UI workflows, native JUCE resource parity, Windows compile/CTest and physical audio FAT remain separate binding checks. A failed visual comparison is not overridden by subjective assertion.

## Performance and qualification

This eliminates avoidable CSS style-resolution declarations in the actual shipped Pages/native GUI without adding runtime JavaScript sampling, CSS overrides, duplicated renderers, or changing PCM/ASIO/WASAPI, meter truth, VST3, PAN, fader, physical routing, and native ACK/readback.

Exact removed-declaration counts, cross-file reductions and SHA-256 digest data are emitted in the generated /preview/build.json -> css_compilation. Build via python scripts/build_pages.py --out dist-pages. The native WebView bundles exactly that generated asset set.

This milestone DOES NOT independently prove physical Windows cursor latency, GPU compositor cost, or audible PCM correctness. Keep #56 open until affected-PC WebView2/ETW traces, pointer feel, p95/p99 and hardware FAT provide repeatable evidence, as documented in the Stage A profiling procedure.
