# Approved v9 MUTE / SOLO / PFL hardware keys — MAIN MIX and SOF

**Owner correction — 13 November 2026.** This contract supersedes the
previous PR #610 rule that put PRE/POST in the Main Mix third-key position.
That caused the user-visible regression: PFL appeared only in SOF. The
operator-approved v9 physical key row is MUTE / SOLO / PFL in the normal
mixer **and** in Sends on Faders.

## Approved PNG, unchanged

- Original MUTE/SOLO/PRE/POST 228 x 90 approved asset remains byte-identical:
  `site/preview-buttons/assets/mixer-keys-thin-flat-v9-approved-off-shadow-57x30.png`
- Approved 114 x 30 PFL OFF/ON FontMatched RGBA sprite remains byte-identical:
  `site/preview-buttons/assets/mixer-keys-thin-flat-v9-pfl-font-matched-57x30.png`
- PFL sprite SHA256: `1ede18534fa678461ebcb1a100a61170700d75048b11607600cf09d9de8872fd`.
- The sole physical key owner is `mixer-control-keys.css`; no CSS substitute
  for PNG, new glow, new button geometry, independent renderer, or extra skin.

## Physical key row

| Workspace | Audio | BUS / FX | MASTER |
| --- | --- | --- | --- |
| Main Mix (default) | MUTE / SOLO / **PFL** | MUTE / SOLO / **PFL** | MUTE / SOLO |
| Sends on Faders | MUTE / SOLO / **PFL** | MUTE / SOLO / **PFL** | MUTE / SOLO |

PFL is not a PRE/POST send tap. The Main Mix no longer installs a global
channel `tapMode` button that can unintentionally retarget *all* parallel
send edges. Send PRE/POST is a **per-EdgeId** routing control in the existing
WIRE Route Inspector; in the contextual SOF implementation (#604 included by
advanced #606), it belongs in the selected target's SEND CONTROLS section.
That experimental branch needs this v9 main-key change forward-ported
before integration; do not merge it over the approved current main.

The existing WIRE connection mode selector and per-edge tap controls remain.
Neither PFL nor switching workspaces may create, remove, or retarget WIRE.

## Truthful audio status

- Browser-only `cue` is a visual selection, **not audio**. Clicking PFL can
  illuminate the exact approved blue PNG, but it does not alter MAIN,
  send topology, faders, MUTE/SOLO, or monitoring sound.
- PFL is visible in both workspaces and its browser selection must survive
  Main Mix <-> SOF changes; there is no hidden stale cue request.
- Native JUCE/WebView2: PFL must be visibly OFF/disabled until a genuinely
  admitted independent pre-fader MONITOR L/R path has an engine ACK/readback.
  Native bridge `consumeAction('cue')` already fails closed.
- This change is a GUI correction only, not a claim of native PFL audio,
  Windows/ASIO FAT, or complete native SOF.

## Tests and publishing

The source/raster contract, the Stage D Chromium browser smoke, public
Pages PNG/WIRE browser smoke, and archive contract must check third-key PFL
in **default Main Mix before any SOF click**, then after SOF toggle, and the
MASTER-only two-key row. Original approved PNG hashes, PFL OFF shadows,
blue PFL ON, ON/OFF hitboxes, and WIRE per-edge controls are protected.
Publish only the assembled `/preview/`; historical `/preview-prod/` remains frozen.
