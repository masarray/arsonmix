# Approved PNG hardware keys — internal build dependency

This directory owns **only** the approved MUTE/SOLO/PFL assets:
- `assets/mute-solo-pfl-56x37.png`
- `assets/mixer-control-keys.css`
- `assets/mixer-control-keys.js`

**The only approved product GUI URL is https://masarray.github.io/arsonmix/preview/**. This directory is NOT a second published preview. `scripts/build_pages.py` assembles the current internal mixer source with these PNG-button assets to create **one** `/preview/` page. Legacy `/preview-buttons/` and bare `/preview-v2/` URLs redirect there, never render a different GUI. Do not freeze another HTML copy or replace approved button material with bare-v2 CSS.

Native Windows WebView2 must embed precisely the same assembled /preview/ GUI. See root AGENTS.md, P0 #389, and `tests/single_preview_publication_contract_test.py`.
