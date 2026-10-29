# R0 — Premium native routing workspace reconciliation (2026-10-29)

Baseline: `main@3ab35d3f3a8f8afef2a5909de531ab98618d61ba`. PR #146 already delivered an ACK-aware real INPUT PATCH matrix with eight visible hardware columns, stereo L/R guards and device epoch safety. R0 **reuses it** instead of shipping another matrix or replacing the accepted live mixer.

## Product implementation
- The routing surface now fills the existing **workbench** below the approved 56px ArSonMix toolbar, beside the 156px channel rail, above the 24px status/footer. No fullscreen dimmer, blocking center dialog, glass blur, unnecessary margin or fake product shell.
- A single source-owned native `monitor.js` style block replaces stacked R1 visual overrides with the same graphite/steel/turquoise/green active semantics as the mixer. No passive hover CSS, per-cell column JS pointerover scan or additional animation/RAF/timer. Keyboard focus remains visible.
- Compact INPUT PATCH tabs, 10 CH × eight paginated physical input cells, LIVE/draft/removed meaning, device port names, and read-only hardware OUTPUT keep native authoritative ACK and fail-closed APPLY.
- INTERNAL / SENDS is **lazy**: 10 channel VST3 dropdowns and 4 AUX VST3 dropdowns are not populated on INPUT open or when catalog status changes while that tab is hidden. It is rendered on explicit selection and otherwise retained with existing 10+4 controls and PRE/POST sends. Full internal send matrix and single shared plugin picker are later R1/R2 optimization gates, not falsely marked delivered here.
- APPLY, stale device detection, MUTE OUTPUT fence, native revision ACK, draft revert, channel values, live FX instance preservation, DRY Show eligibility, and all engine files are untouched. Escape and toolbar navigation close the workspace without changing routes.

## Validation
- Chromium native-bridge smoke adds precise workspace geometry and screenshot-compatible checks at 1680×920 and 1440×820, verifies no eager INTERNAL construction and inert hover, on top of the existing R1 matrix, ACK, stereo and device swap tests.
- Native routing Node contract checks workspace region, preserved ACK and absence of hover hooks.
- This is an independent GUI PR; Windows CI stays on canonical main/feature pipelines. Real ASIO/WASAPI FAT, WebView2 low-GPU p95 input-to-paint, and physical no-crackle qualification remain separate from browser smoke.

No changes to `web/styles-src`, `web/app.js`, `src/Main.cpp`, callback audio code, Plugin Vault, or any VST3/Orbit processing.
