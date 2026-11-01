# ArSonMix V8 — Adaptive Bank Geometry and Collapse Integrity

Date: 2026-11-02 | Owner-selected isolated GUI reference PR #278 | GUI only

## Regression addressed

1. **Density was partly cosmetic:** a single hardcoded 130px source-bank divisor ignored Comfort / Standard / Compact and could compress an intended senior-accessible channel below its chosen minimum.
2. **Bank width used the wrong rail budget:** width was calculated with 28px even though the actual console rail is 32px; source strips and pinned MAIN could differ slightly.
3. **Create Channel / FX Return / Bus could select an offscreen channel:** horizontal scrollbar is intentionally suppressed, so the prior `scrollLeft=99999` did not reveal newly created items.
4. **Collapse visually clipped children instead of hiding them:** expanded-specific `display:grid!important`/`display:block!important` declarations overrode earlier collapsed rules for Route and Send. The 23px collapsed area showed partial controls that looked overlapped.

## Implemented

- `DENSITY_WIDTH={comfort:150,standard:125,compact:105}`; bank capacity is bounded by an actual section-rail-adjusted width budget, with 2px total subpixel tolerance near the 1680px breakpoint. Maximum **10 channel slots + one pinned same-width Master**, never a giant single last-bank strip. Remaining slots on a partial bank stay reserved.
- `bankGeometry()` is the single source of available width for capacity and the pinned MAIN CSS variable; a floating Plugin Manager does **not** decrease bank capacity.
- `setDensity()` retains the leftmost visible channel as a bank anchor as page size changes; channel reorder is not performed.
- `revealTrack()` makes a newly created channel visible on its correct bank before presenting it as selected; user filters revert to All if necessary to reveal the newly created role.
- Targeted final CSS rules hide Route fields / FX rack / Send controls on collapse rather than merely clipping their container. The rail and source sections share the exact `--route-h`, `--fx-h`, and `--send-h` tokens.
- Existing pointer-capture fader behavior, legacy hardware artworks, icon palette, channel identity, FX picker, M/S/PFL, Sends on Faders, Group, snapshots and one-window Manager remain untouched.

## Verification performed

- **150/150** zero-dependency smoke assertions passed in the JavaScript harness, including new 1366px geometry scenarios:
  - Comfort: seven source strips plus Master; `--channel-w` ≈ 166.75px.
  - Standard: nine source strips plus Master.
  - Compact: ten source strips plus Master.
  - 1180px Comfort: six readable strips plus Master.
  - 1680px Comfort: ten source strips plus Master at ≈ 149.82px (within 2px total rounding tolerance).
  - Newly added audio, bus and FX Return visible on their bank.
  - Collapsed content rules are present with higher CSS specificity and `!important`.
- This is **DOM, source and deterministic width-budget verification**, **not** rendered Chromium screenshot or OS/native evidence. CSS geometry still needs a rendered screenshot review at 1366×768, 1680×900, and 1920×1080, with Route/FX/SEND individually and jointly expanded; compare against operator screenshot.

## Native contract and publishing

GUI reference only; do **not** merge the entire isolated prototype into production `web/**`, call demo meters live, or claim genuine VST3/ASIO. ARD v5.2 §K and PRD v5.2 §9 remain normative. Publish isolated source under `/preview-v2/` for visual review, keeping `/preview/mixer.html` and actual native production files untouched.
