# Brushed Metal v1.0 — Frozen Visual Reference and Migration Handoff

**STATUS: exact approved styling source preserved; NOT production theme runtime code.**

## The original CSS is already here

- **Exact source:** [adapter.css](adapter.css) — *14,031 UTF-8 bytes*, Git blob `1086c285a44d2d06787a12ae809c1d4c5c80bb44`.
- **Content checksum:** SHA256 `d4d3b13fa5ceba0f29665f902735ad0e48c4df44e4107cb00d2ff2f65ec678b6`.
- **Source identity:** [reference.json](reference.json) — upstream GitHub `3478823069d053db4f0d4c34786f444df3defac0`, original canonical standalone source blob `abbc4916be3af9ea73d6cb60f4537ac366d0dd49`.
- **Preview assembler:** [scripts/render_brushed_reference.py](../../scripts/render_brushed_reference.py).
- **Regression test:** [tests/theme_reference_fixture_test.py](../../tests/theme_reference_fixture_test.py).
- **Architecture & execution:** [ARD](../../docs/ARD_MULTITHEME_ENGINE_V1_2026-10-26.md) · [T0–T7 roadmap](../../docs/THEME_ENGINE_IMPLEMENTATION_PLAN_2026-10-26.md) · [Engineer handoff](../../docs/THEME_ENGINE_NEW_THREAD_HANDOFF_2026-10-26.md).

This is the **last accepted Brushed Metal skin**, carried forward **verbatim**, not a reinterpretation or hand-written replacement. The original was composed as a CSS adapter on the published standalone HTML from commit `3478823` while leaving JS unchanged.

### Recreate pixel-equivalent historical reference

From the repo root:

```bash
python tests/theme_reference_fixture_test.py
git show 3478823069d053db4f0d4c34786f444df3defac0:AR-MIX-Live-FX-Host-Standalone-v0.3.html | \
  python scripts/render_brushed_reference.py --source - --strict-original \
    --output dist-theme-previews/brushed-metal-v1.0-original.html
```

Open the resulting HTML in Chrome/Edge. It is **only** a reference preview. No GUI source, native bridge, audio DSP, or `main` changes are performed by this command.

### Compare Brushed Metal against a new GUI revision

```bash
python scripts/build_ui_css.py --check
python scripts/render_brushed_reference.py \
  --source AR-MIX-Live-FX-Host-Standalone-v0.3.html \
  --output dist-theme-previews/brushed-metal-latest-compatibility.html
```

The generator warns when the input is not the historical 3478823 source; the result is then a **compatibility experiment**, not a guarantee of visual parity. If markup changes, it is expected that the historical adapter may fail visually; that is exactly why production theming must use semantic component tokens.

### Critical appearance identity (nonnegotiable)

| Component | Accepted look | Keep out of |
| --- | --- | --- |
| Toolbar + outer chassis + channel/control surface | Silver/brushed aluminium, subtle horizontal grain | Dark crystal theme CSS |
| MUTE/SOLO/Ø/HPF, rail/header/master buttons | One physical aluminium face; active state changes ink/ring/glow **only**; compact bevel; no motion/pressed displacement | Altering hitbox/layout |
| FX insert slots and FX power | **Black-blue, glossy/shiny premium glass**, power indicator cyan/bypassed muted | Silver metal button selectors |
| FX RACK workspace + plugin browser | Blue-black glass | Metal surface |
| PAN | Center-zero blue fill behind canonical metallic PAN PNG | Geometry or bitmap replacement |
| Fader | Canonical silver fader, master red; no jump when dragging | Theme-specific placement or scaling |
| dB / LUFS-I / dBTP / meters | Dark high-contrast display | Silver metal LCD |
| MUTE/SOLO/HPF semantic states | Red/amber/green; phase blue | Generic recoloring that loses meaning |

`adapter.css` uses `#app`, selectors, `!important`, and a few **geometry declarations** (`height:29px`, PAN top, padding/gap). Those choices were acceptable for the historical **reference** only. Never install the full adapter in `web/styles-src/60-theme-tokens.css`, never duplicate it for a Default theme, and never call it a maintainable token module. **Mine its visual values** and transfer them one consumer component at a time, preserving current GUI layout.

### How to implement without coding CSS from scratch

1. First run T0 snapshot on newest main, keep the unmodified `studio-default` pixel baseline.
2. Categorize every selector/declaration in `adapter.css`: *material state* (migrate), *component optics* (migrate renderer), or *geometry/mechanics* (**reject** from theme).
3. Populate semantic visual tokens with **the actual existing colors, gradients, shadows and glow values**. Use reference CSS as the data source; do not redesign buttons.
4. Refactor the currently owning components (`20-button-180.css`, canonical FX material, rail, meter and PAN renderers) to consume tokens with default fallbacks.
5. Add static precompiled `studio-default` and `brushed-metal` token manifests. Theme switch sets only a root appearance attribute.
6. Ensure FX glass has **its own component renderer/token family**. Never inherit global brushed button optics into insert/power.
7. Check default against golden computed styles and every theme against geometry invariants. Only then add accessible Appearance selector.

If a layout is changed later by GUI Core, tokens cannot alter spacing, DOM, width, height, z-index, hitbox, or native events. New controls get an explicitly reviewed semantic component role and safe default fallback.

**Never bring back the previous brittle approach:** `button{...!important}`, 600KB copied standalone per theme, last-file wins override stacks, CSS layout patching with `top`/margin, duplicate event listeners, or theme-triggered `renderMixer()`/audio bridge sends.
