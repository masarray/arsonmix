# ArSonMix GUI checkpoint — V51 Meter LOCK / V52 Fader Well

Approved by owner in the ArSonMix design-review chat, 2026-11-04: **"OKE METERS SUDAH BAGUS -> KUNCI CHECKPOINT PROGRESS"**.

## Immutable approved reference

- Main checkpoint commit: `885e7e8175c0f0cab288760d3cd60c0e77e04812` (merged GUI V51)
- Pinned Git ref: `checkpoint/gui-v51-meter-approved-20261104`
- Frozen metering component: `site/preview-v2/assets/mixer-meter-display.css` Git blob `cea7a411afea90b26557f317ba93c563dd35379c`
- Frozen V51 JS: existing stepped dBFS quantization, independent L/R per-channel peak hold, release, clip latch and demo preview adapter in `site/preview-v2/index.html`. **Do not revise** in this milestone.
- `tests/pages_v51_portrait_led_peak_hold_smoke.py` is mandatory for future GUI work.
- Preserve approved 70px meter housing, 10px lane, 6x12px portrait optical LED / 14px pitch, gradient and position-color-matched peak hold.

## V52 correction: fader material only

The dark slot must read as a mechanical opening in a **recessed instrument pocket**.
The center is flat, surrounded by narrow, deliberate inward bevel shading.
The neighboring dB gain ruler must be painted on exactly the same lighter flat metal
chassis as the surrounding fader workspace: `#263944`.
No dark gradient backing behind the ruler to compete with the actual well.

| Density | Existing cap art | Approved pocket width | Delta |
| --- | ---: | ---: | ---: |
| Comfort | 27px | 29px | +2px |
| Standard | 27px | 27px | 0px |
| Compact | 25px | 25px | 0px |

Use only the existing fader CSS owner, approved `--ari-silver` /
`--ari-rose` thumb PNGs and existing pointer-to-dB model.
A 5px almost-black slot remains mechanically centered; the existing
low-level gain feedback is preserved but deeply darkened.
The thumb carries **two fixed, small CSS box-shadows** whose lower edge
is visible in ordinary mix positions. No `filter`, `drop-shadow`,
CSS animation, `backdrop-filter`, new DOM, timer, React, or audio/DSP work.

## Acceptance / non-regression

- Run `tests/pages_v52_fader_recess_smoke.py` in Chromium at
  1366x768, 1680x900, 1920x1080 across Comfort, Standard, Compact,
  plus pinned Master and pointer drag.
- Re-run V51 peak/portrait meter regression, M3 fader/meter geometry,
  CSS ownership historical snapshots (on pinned V24 fixture), and UI smoke.
- Verify V51 meter blob identity before live GUI testing; no rebaseline.
- Preview: `https://masarray.github.io/arsonmix/preview-v2/` **only after**
  the GUI branch is reviewed, CI passes and main Pages publishes successfully.

**Scope:** HTML/browser visual reference. This checkpoint is not evidence of
native Windows PCM, ASIO or VST3 hosting; unrelated ENGINE/integration PRs remain isolated.
