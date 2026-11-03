# CSS V25 — Rehome original V30 physical-key material without visual changes

**Approved LIVE BASELINE** `885e7e8175c0f0cab288760d3cd60c0e77e04812` (GUI V51 PR #355 merged, main Chromium UI #41717540551 PASS, Pages #41718978183 PASS). The current GUI is visually locked; do not redesign, normalize, or rerender its meter, buttons or WIRE.

## Root cause / one deterministic owner boundary

The last 8,746 characters of `site/preview-v2/index.html`'s inline CSS still contained the approved V30 physical satin keys, the shared toolbar/physical press/hover/focus material, and its small responsive/reduced-motion guards. The existing `mixer-control-keys.css` is loaded **immediately following** that inline block.

V25 moves that **complete contiguous suffix**, without modifying a character, to the **start of the existing key stylesheet**. This is a single source migration, no style patch, no new stylesheet, and no geometry or material redesign. The existing V6 modern MUTE/SOLO/PFL subowner remains **byte-for-byte intact** after the moved prefix. Historical V30 legacy `.switch` rules may be dead in the modern DOM but are deliberately preserved until there is independent evidence that removing them is safe. The 4 trailing responsive/reduced-motion guards remain exactly in their prior cascade order and are earmarked for a future owner split with evidence, not silently rerouted.

**Critical formal guarantee:**

```
OLD inline CSS + OLD mixer-control-keys.css
    ===
NEW inline CSS + NEW mixer-control-keys.css
```

The concatenated CSS texts are **identical byte-for-byte**. No `url()`, `@import`, `@charset`, CSSOM `document.styleSheets` manipulation or modern `.mx-hw-key` selectors exist in the migrated slice. CSS file positions adjacent in HTML remain fixed; no shift across any FX/Meter/WIRE/Contextual stylesheet.

| Measure | V51 original | V25 |
|---|---:|---:|
| Inline physical styling characters | 23,140 | 14,394 |
| Existing control keys stylesheet characters | 7,997 | 16,743 |
| Migrated physical material declarations `!important` | 97 in inline | 97 in existing key owner |
| Actual CSS declarations deleted | 0 | 0 |
| Added `!important`, CSS files, JS/ENGINE changes | 0 | 0 |

**These numbers are a real 8,746-character inline ownership migration, NOT CSS byte savings.** V22/V24 real duplicate pruning is already complete.

## Engineering safeguards

- `tests/fixtures/css-v25-physical-key-owner-migration.json`: exact moved 8,746-character CSS source, hashes, codepoint sizes, cache URLs, prior/live HTML and owner fingerprints.
- `tests/css_v25_restore.py`: recovers the **exact V51 pre-V25 HTML and key owner** and checks the unchanged full CSS sequence. No previous migration ledger rebaselined.
- V25 Chromium: five viewports × eight collapsed rail states, three viewports physical material/pseudo/interactive/state/design tokens, **nine density+V51 meter/peak-hold/fader/WIRE computed style snapshots**.
- CI pinned V24 historical gate now also saves/restores the control keys owner so past historical test pages run only against their original CSS; the current V51/V25 owner is restored before all user-facing control smoke tests.
- Current GUI V51/V50/Contextual tests run first; V25 new test after historical test restores; existing MUTE/SOLO/PFL and all WIRE/native-bridge GUI smoke remain required. Browser Pages asset cache changes only for the moved key owner.
- `tests/pages_controls_v6_smoke.py` retains the **original V6 14-`!important` limit**, measuring the exact recovered modern section instead of incorrectly treating the preserved V30 historical material as newly-added overrides.

## NEXT: CSS V26

There are still 14,394 characters of late inline V15/V16 WIRE and V19/V22 visual layout contracts. **Do not bulk-extract** them to arbitrary components or reorder stylesheet links: their logical owner is earlier/later than the inline block. Next audit their computed winning properties and group by physical WIRE, rail and header boundary; separately evaluate global `html *` selectors for modal/Plugin Manager leakage. No removal or relocation without the same source-order proof and real browser parity.

Visual integrity takes precedence over line count.
