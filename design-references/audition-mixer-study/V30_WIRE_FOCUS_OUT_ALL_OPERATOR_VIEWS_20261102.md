# ArSonMix V30 — Optional WIRE View Modes (GUI Reference)

Date: 2026-11-02. Implementation: isolated design reference in PR #278; **not native audio routing**.

## Operator behavior

- **Focus (default):** selected channel incoming cables behind its brighter outgoing cables, as approved in V26. Compact channel-colored IN/OUT legend is unchanged.
- **OUT only:** shows outgoing cables from the selected channel, deliberately hides incoming paths without unlinking them.
- **All wires:** adds only unrelated routes whose endpoints both exist in the currently visible bank as very faint context behind selected-channel incoming and outgoing. Hidden-bank routes are never fabricated across empty strips; selected channel's IN/OUT legend still reports links to other banks.
- Collapsing WIRE empties the cable SVG regardless of mode, preserving the approved 23px DOT/socket status and no overflow into PAN.
- Mode control occupies just 77px in the existing header; no new rack, panel, fader movement, or routing editor.
- View change does **not** rebuild channel strips, update `wireEdges`, touch sends or source/MAIN output, or create a new graph transaction.
- Reset restores Focus; expanded routing supports the same drag/snap/unplug paths.

## Acceptance gate

1. Zero-dependency model contract in `smoke.test.cjs`, including default, display-only mode changes, layer order and reset.
2. Dedicated Chromium Playwright test `visual_wire_v30_views_qa.py` at 1366×768, 1680×900 and 1920×1080: four actual routing fixtures, default Focus 2-IN/1-OUT, OUT 0-IN/1-OUT, ALL one faint unrelated route plus focus, unchanged socket/fader state, collapsed zero-cable SVG and expand restoration.
3. The reference-only PR GitHub Actions workflow runs the V30 test after V29. CI outcome must be verified separately. No production engine/pixel/PCM qualification is claimed.
