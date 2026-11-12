# Harmonice v9 physical MUTE / SOLO / PFL — SOF integration contract

## Artwork provenance and nonnegotiable look

Use the user's approved **Harmonice_Thin_Flat_Bezel_v9_PFL_FontMatched_Assets.zip**.
The blue PFL PNG master has the same thin flat bezel, cap geometry, typography
and ON text debossing as the v9 MUTE/SOLO faces. Do not recreate it in CSS/SVG.

- Existing production 4×3, 57×30-per-cell MUTE/SOLO/PRE/POST atlas remains
  **exactly unchanged**:
  `site/preview-buttons/assets/harmonice-thin-flat-v9-approved-off-shadow-57x30.png`.
- New approved **PFL OFF / PFL ON** raster is a separate 2×1 114×30 RGBA atlas:
  `site/preview-buttons/assets/harmonice-thin-flat-v9-pfl-font-matched-57x30.png`.
- PFL sprite SHA256: `1ede18534fa678461ebcb1a100a61170700d75048b11607600cf09d9de8872fd`.
- Display through the same `mixer-control-keys.css` and existing
  `strip(t)` renderer; the artwork remains PNG, never a CSS imitation.
- PFL OFF inherits the two approved v9 lower-alpha drop shadows; PFL ON does
  **not** inherit OFF shadow. Neither frame alters button hitbox/strip height.

## One workspace decides the third physical key

| Context | Source audio strip | BUS/FX strip | MASTER |
| --- | --- | --- | --- |
| MAIN MIX | MUTE, SOLO, PRE/POST | MUTE, SOLO, PRE/POST | MUTE, SOLO |
| SOF | **MUTE, SOLO, PFL** | MUTE, SOLO | MUTE, SOLO |

PFL is **not** a replacement for PRE/POST routing. In SOF, the selected
send's PRE/POST belongs in the contextual **FX/send processing panel** (PR
#604); its hardware key row uses PFL. The PFL key does not change the
SOF target's send tap/gain or the channel's PRIMARY MAIN edge.

## Monitoring and state contract

- Browser reference: PFL uses the existing per-channel `cue` state, purely
  as visual feedback. The browser generates no real monitored audio and
  **must not feed cue into MAIN**. PFL is initially OFF.
- Leaving SOF clears browser-only PFL requests; there must be no invisible
  armed cue. Normal MUTE/SOLO survive workspace changes.
- Native: PFL is disabled / visibly OFF until a separate pre-fader MONITOR
  L/R bus has a real native route and ACK/readback. The existing native
  bridge `consumeAction('cue')` fails closed; the button must never simulate
  monitor audio or claim native availability.
- Native SOF itself remains unavailable until native send gain/tap support is
  admitted. This PR does **not** implement an audio PCM audition bus.

## Concurrent work: draft PR #604

**Do not overwrite or independently merge #604's experimental SOF branch.**
The SOF author must forward-port the third-key decision from `strip(t)`:
`sofPfl = sofEnabled && t.kind==='audio'`, `hasTap = t.kind!=='master' &&
!sofEnabled`. When SOF is active, preserve PFL in the MUTE/SOLO key row
and keep per-EdgeId PRE/POST in the contextual SOF panel. Preserve the one
existing key CSS owner, both approved raster sprites and native fail-closed
behavior. Do not restore PFL into MAIN MIX or the hidden Clear PFL toolbar.

## Regression evidence

- Fast `tests/harmonice_v7_keys_contract.py` checks SHA, dimensions, the
  existing MUTE/SOLO sprite, renderer contracts and native guard.
- Existing Chromium Stage D checks both contexts, PFL OFF→ON without layout
  changes, actual sprite URL/frame/shadow, no MASTER PFL, and reset on exiting
  SOF. It does not assert any native PCM audio.
- Publish only the single `/preview/` GUI; `/preview-prod/` is a frozen
  pre-v7 comparison and must remain unchanged.
