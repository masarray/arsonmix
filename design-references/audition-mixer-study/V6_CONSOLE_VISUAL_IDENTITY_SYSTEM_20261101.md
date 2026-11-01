# ArSonMix V6 — One Console / One Material: Visual Identity System

Date: 2026-11-01 | Isolated PR #278 | Main Preview export route `/preview-v2/`

## Visual authority and nonnegotiables

**Visual material:** historical `web/styles-src/20-button-180.css`, old mixer VU/fader PNG and RAIL3 geometry, and `web/app.js` `channelIcons`/`channelPalette`/`strip-name` identity. **Workflow authority:** new operator-first ten-channel bank, top bank arrows, FX direct insertion, PFL and snapshot. The old DAW editor/timeline and automation workflow remain out of scope.

Previous V5 had a split personality: the upper route/FX looked like dark web-form tables while the lower fader area resembled machined hardware. V6 adopts **one graphite/navy metal chassis and one crystal/plastic button material** across the complete channel height. Bright chromatic blocks are reserved for **channel identity** at both ends, meter/signal/clip states and active command states; they must not be repeated on every FX row. No oversized font/card or unsupported GUI decoration.

## Theme tokens (implemented in the V6 HTML)

| Role | CSS token | Color | Visual behavior |
| --- | --- | --- | --- |
| Backplate | `--am-metal-base` | `#0B161E` | Negative space between strips |
| Channel panel | `--am-metal-panel` | `#14232C` | Route / sends / FX tonal unity |
| Elevated tool surface | `--am-metal-raised` | `#20313B` | Top toolbar and popovers |
| Fine divider | `--am-metal-edge` | `#334956` | One-pixel borders; no fat frames |
| Machined opening | `--am-metal-inset` | `#030A0F` | Realistic slot, no cyan glow |
| Primary text | `--am-text-primary` | `#DAE9EF` | Readable but non-glowing text |
| Secondary text | `--am-text-muted` | `#94B1BD` | Only supplemental metadata |
| Interactive focus | `--am-accent` | `#66CBDD` | Focus/selection, not ambient glow |
| Off-state plastic | `--am-crystal` | `#E7EFF0` | Brighter off BTN-180, same original 7-shadow recipe |

## Channel identity (copied from historical app)

Top nameplate: **CH XX / channel name / icon**. Bottom plate: **channel name / icon**. Both share `--ch` derived from the track's stable `color` field and use the same color-mixing ratio/graphite support. The extra top band has its own matching left section-rail band, preventing the common misaligned-section regression. The entire strip remains equally sized, including the pinned MASTER and incomplete CH11+ banks.

Original approved icon list (in order): `♩ ♫ ♬ 🎙 🎸 🎹 🥁 🎧 ▥ ◉ ◎ ⊞`.
Original approved palette (in order):
`#1699CD #B92E41 #CF6B1E #B28C27 #34A544 #168762 #0E95C9 #066FA9 #7140B5 #B52893 #C32974 #586889`.

Click either channel identity strip to edit **name, icon and color** via one lightweight anchored popover (same operation pattern as the old `openChannelIdentity()`). Rename updates output and every send-slot route referencing that name, avoiding a silent broken routing graph. Duplicate/empty channel names rejected. Current session fields are preserved by snapshot copy/recall.

## BTN-180 and toolbar rule

- `web/styles-src/20-button-180.css` original surface is already bundled in the reference. V6 brightens **OFF only** to `#E7EFF0`, preserving edge color, inset/outside shadows, 1px pressed travel, and semantic ON faces (MUTE red / SOLO yellow / PFL cyan).
- Apply to all header **buttons**, including More menu. Keep selects as dark inset console controls. Avoid fake VST/DAW controls.
- All header controls have the same **29px height** and are center-aligned against the 44px toolbar. Text line-height is 1; SVG bank arrow is vertically centered as a 20px icon. Preserve the user's exact SVG path data: left `M14,17L9,12l5-5V17Z`, right `M10,17V7l5,5l-5,5Z`; use `currentColor` so disabled/hover color is consistent.
- Header state color only appears on active `aria-pressed` actions; route/FX forms remain quiet in dark metal.

## Quality gates

Passed: **132/132** existing and V6-specific zero-dependency DOM/business interaction checks, including original 12-icon palette, same-color top/bottom identity, exact arrow paths, toolbar BTN-180 styling, live Bus rename routing migration, original fader pointer drag and fast FX picker. Embedded JS parses; CSS braces balance.

**Not certified:** pixel screenshot equivalence on Chromium at 1366/1680/1920, perceived color contrast on user hardware, native realtime VST3 and true meter precision. V6 is an HTML GUI reference, not an ASIO audio engine. Keep PR Draft and separate from production GUI/engine threads until a native integration gate.

## Preview acceptance checklist

At desktop size: 10 source strips + one pinned Master equal-width, stable section edges, header buttons centered, channel bands same exact hue on top and bottom, 29px aligned 3-way MUTE/SOLO/PFL; FX Add opens fast picker without Plugin Manager; change icon/name/color via identity editor and see both nameplates update; group/sends/snapshot retain behavior; pan center snaps; fader drag updates the cap and LCD. For CH11/Bus A arrow, never widen one strip. Verify no bottom horizontal scrollbar and no clipped IN/OUT.
