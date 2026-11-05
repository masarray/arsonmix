# ArSonMix Stage C — CSS Cascade Optimization and Visual-Parity Gate

**P0 pointer lag issue:** https://github.com/masarray/arsonmix/issues/56  
**Integration:** https://github.com/masarray/arsonmix/pull/414

## Runtime milestone

The ONE public `/preview/` GUI and the matching JUCE WebView2 resource use a **deterministic CSS source compiler** in `scripts/compile_approved_css.py`, executed only by `scripts/build_pages.py`.

The compiler inspects the approved 18 ordered CSS owners. For matching *exact selector, property, priority and at-rule context*, it retains the effective cascade winner and omits earlier redundant declarations **from the deployed copies**. It does not add a late stylesheet, move CSS between owners, edit the accepted buttons' PNG assets or replace the UI renderer. URLs are content-addressed for browser-cache safety.

On the checked Stage C implementation, build logs report **242 declarations removed, of which 89 were cross-file**. The authored CSS source still contains its historical debt; source hashes are deliberately preserved because legacy V28 source restoration tests rely on those bytes.

Two especially sensitive assets — `mixer-control-keys.css` (approved physical PNG buttons) and `mixer-wire-dots.css` — remain byte-identical even in the built output. No new `!important` overrides are introduced.

## Proof required

- `tests/approved_css_stage_c_compiler_test.py`: source-preserving compiler unit tests; cascade priority and nested conditions; string/comment parsing; locked hardware controls.
- `tests/single_preview_publication_contract_test.py`: original source + approved PNG revision + compiled hashed CSS is **exactly** the sole published /preview/, with deterministic owner hashes; no independent preview-v2.
- `tests/approved_stage_c_css_visual_parity_smoke.py`: screenshot and computed style comparison between original and deployed CSS, including hover states.
- `tests/approved_css_stage_c_computed_parity_smoke.py`: independent deeper computed style + geometry comparison across 1366×768, 1680×900, 1920×1080; idle, strip hover, hardware key hover and MUTE press. The only normalized difference is absolute *test folder* prefix for asset file:// URLs. It never normalizes numeric styles, colors, CSS filenames, URLs' trailing paths or dimensions.
- The inherited CSS V28 exact-source and full WIRE/fader/meter visual regression suites remain enforced; Windows JUCE/CTest/EXE must also succeed at the final PR SHA.

## Technical debt and what is NOT proven

The existing source inventory shows **114 cross-owner selector/property overlaps and 252 within-owner repeats** under its conservative static counter. That does **not** equal the number of independently removable browser declarations: priority, scope, fallbacks, PNG/WIRE preservation, and vendor compatibility limit safe pruning. Do not advertise "zero CSS duplicates" in source; the new compiler removes only validated runtime losers, while gates forbid adding new ones.

Actual mouse input-to-photon latency and GPU compositor performance on the affected Windows machine have **not** been measured. A successful Chromium/Windows CI cannot certify physical cursor smoothness, ASIO/WASAPI playback or full hardware FAT. Keep issue #56 open and capture Stage A evidence on the user's machine before claiming a stutter fix.

## Maintainability rule

Do not append an override stylesheet or an additional `!important` patch. Author future UI changes in the existing owner, maintain one deterministic compiler output, preserve link order, and first demonstrate exact compiled CSS parity plus Stage A+B runtime behavior. Reduce the remaining source debt separately with a deliberate checkpoint migration, never by weakening historical fixture checks.
