# Approved PNG hardware keys — internal build dependency

This directory owns **only** the approved MUTE/SOLO/PRE/POST assets:
- `assets/harmonice-thin-flat-v8-57x30.png`
- `assets/mixer-control-keys.css`
- `assets/mixer-control-keys.js`

**The only approved product GUI URL is https://masarray.github.io/arsonmix/preview/**. This directory is NOT a second published preview. `scripts/build_pages.py` assembles the current internal mixer source with these PNG-button assets to create **one** `/preview/` page. Legacy `/preview-buttons/` and bare `/preview-v2/` URLs redirect there, never render a different GUI. Do not freeze another HTML copy or replace approved button material with bare-v2 CSS.

Native Windows WebView2 must embed precisely the same assembled /preview/ GUI. See root AGENTS.md, P0 #389, and `tests/single_preview_publication_contract_test.py`.

## Harmonice v7 PRE/POST semantics

The approved **57×30px** PNG atlas consists of PRE-OFF/POST-ON, MUTE-OFF/ON and SOLO-OFF/ON (two columns × three rows). Its shape, flat bezel, shadow and caption are raster; no pseudo-CSS can repaint the cap.

PFL is temporarily hidden in the strip only; existing cue state/monitor contracts remain. PRE/POST selects the source channel's outgoing **parallel send** WIRE tap, not its primary MAIN route. POST follows the fader (`mode=send`) while PRE is independent pre-FX (`mode=split`). Current native PCM does not admit WIRE MODE retargeting, so native UI must fail closed rather than depict an unacknowledged audio change. Browser `/preview/` demonstrates reference routing, not real PCM.

## Harmonice v8 visual refinements

No outgoing parallel SEND: POST is rendered as a **non-illuminated graphite PNG**; this is the default for future wires, never a claim of active audio. A routed, uniform POST SEND displays a softer green PNG; PRE uses the original approved raised metal frame. Mixed SEND modes display a separate neutral **MIX** PNG, never an invented single tap state. Original MUTE, SOLO and PRE cells preserve their v7 pixels. PFL toolbar action is hidden as well as strip PFL. MASTER has only MUTE/SOLO, centered. These four visual states are computed from existing WIRE edges, without introducing a second state authority or extra CSS glow.
