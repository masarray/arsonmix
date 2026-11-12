# Guitar FX editor design study and original hardware assets

**Not the ArSonMix published mixer. Not an audio processor. No native hardware or PCM claims.**

This folder is an internal visual/interaction study for a future **single native Guitar FX insert** hosted by the already-approved ArSonMix /preview/ mixer. Its hardware-looking PNGs are original procedural drawings, not photos, and must undergo visual review before product promotion. Core contract: [Guitar FX Foundation](../../docs/guitar-fx/GUITAR_FX_FOUNDATION_20261113.md).

## Try the workflow locally

1. Clone this branch, then open `design-references/guitar-fx/editor-prototype.html` in Chrome/Edge.
2. Select Clean/Blues/Crunch/Rock/Metal/Ambient in the preset library; browser can collapse.
3. Choose a chain block to focus its *only* hardware editor; use its power dot to bypass independently; drag movable pedals to reorder while AMP/CAB remain paired.
4. Move knobs vertically or use the accessible range controls; switch A/B; download *design-state* JSON; open Tuner to see the explicit no-audio placeholder.
5. No controls send native commands or generate audio. The CAB mic position is a placeholder only; no fake acoustic claim or live metering.

## Rebuild PNGs and verify

```sh
node scripts/build_guitar_fx_assets.mjs
node --test tests/guitar_fx_assets_contract.test.mjs
```

Requires Node 18+; uses only built-in JavaScript, no image download, UI screenshot, paid fonts, external brands or pipeline dependencies. The renderer generates all PNG bitmaps byte-identically. The original indexed transparent assets are deliberately compact; CSS/HTML owns legends, values, focus, hit targets and state, not pixels. This preserves accessibility and translation potential.

## Design integration constraints

- Exactly one authoritative /preview/ console and one JUCE native graph; this file is not deployed as a competing mixer GUI.
- Integration must use the existing FX editor owner, NativeBuiltInFx registry and accepted snapshot path; coordinate with active #605 FX editor and #604 toolbar PR.
- PNG artwork is decorative and never grounds live meters, model tone, impulse responses or physical signal routes.
- Do not ship NAM models, amplifier capture files or third-party IRs without separately confirmed terms and exact approved binaries.

See original image files under [assets](assets/) and source generator at [scripts/build_guitar_fx_assets.mjs](../../scripts/build_guitar_fx_assets.mjs).
