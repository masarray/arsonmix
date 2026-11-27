# V6 visual consistency and operator menu audit — 2026-11-27

Basis: owner-approved local ArSonMix_Clean_Ribbon_V6.html, existing canonical 32acb5ee, parity PR #984. The local GUI's mixer area was a static reference, not a second product renderer. Scope this change to actual ribbon and application menu; retain approved hardware PNGs and native actions.

## Findings and changes
- Legacy More panel exposed channel, WIRE, safety, device recovery, health and Show storage in one vertical list. The new root has three existing project file actions, five category links and the native emergency command. Category page retains original DOM controls and event handlers, includes Back, Escape, and bounded scrolling. No fake Save As or unsupported preferences/help actions.
- Old CSS owners imposed uppercase, inconsistent select chrome and fixed old dimensions. Use V6 11px regular command labels, 12px menu labels, 25px rounded selectors, consistent pill add/return actions, satin Player states. No global selector changing strip controls or plugin surfaces.
- Technical notes are in the respective category, not the root menu. Diagnostics remains reachable; actual copy report event exercised. Native action labels and safety admission remain authoritative.
- Existing native window caption and full editor visual consistency remain outside this patch; no claim of complete visual parity or production FAT.

## External references (interaction principles, not new visual skin)
- Apple HIG Layout and Toolbars: progressive disclosure, related command grouping, consistent controls. https://developer.apple.com/design/human-interface-guidelines/layout and https://developer.apple.com/design/human-interface-guidelines/toolbars
- Microsoft Fluent 2 Menu: related secondary commands in submenus. https://fluent2.microsoft.design/components/web/react/core/menu/usage
- Waves eMotion LV1 Classic: distinct mixer, show, patch, setup workspaces; channel/session menu distinction. https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html

Acceptance: screenshot actual assembled GUI at 1680/1366/1200/1100, root <=300px, all five category routes, Escape and original file/diagnostic actions. Compare visual against local V6 before merge. No new installer or Windows build for this source change.
