# ArSonMix V13 — Flat Metal / Fixed Lower Console
2026-11-02 • Source authority: PR #278 GUI preview
## User-facing acceptance
The console's PAN, MUTE/SOLO/PFL, FADER, and footer constitute one immutable lower deck. No ROUTE, FX, or SEND expand/collapse action can move or resize any of those lower components. Every upper panel combination is permitted; all spare space stays above PAN. FX takes upper surplus when open, SEND takes it when FX is collapsed. When all upper sections are collapsed, the blank upper budget remains above PAN, not inside the fader.
Fader travel is responsive to viewport height only, never to toggled upper sections. The 1366×768 constraints require a shorter fixed travel than a 1711×1005 workstation because four FX rows, both ROUTE and SEND fully expanded, and all lower controls cannot physically occupy the same pixels otherwise.
## Material language
Clean flat satin-graphite compartments: **no large gradient across metal panel surfaces**. Depth is conveyed only by compartment edges, very thin inset lines, actual meter window and fader track recess. Retain true channel identity colors, original fader/PAN assets and functional level-meter color indicators. Remove fake worn metal, hazy broad highlights and intrusive reflections from panel faces.
## QA invariant
Chromium must validate all 8 combinations of ROUTE/FX/SEND, CH+MAIN and rail, across 1366×768, 1666×888, screenshot-matching 1711×1005 and 1920×1080. Every lower coordinate and height stays within 1.2px; upper rows remain aligned; all-expanded FX shows >=4 rows; main strip surface and all section plates report no CSS background-image. Public preview is built byte-identically from source. Browser demo is not real audio / ASIO / VST3 confirmation.
