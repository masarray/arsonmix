# ArSonMix V17 — Routing UX Optimization Audit and Preview Handoff

Date: 2026-11-02. Scope: PR #278 isolated HTML routing reference, **not native PCM/VST3 integration**.

## Implemented / retained

- **Stable patch row**: fixed 24 / 28 / 22 / 14 px OUT / IN / gain / disconnect columns (plus gaps). No linked-state layout shift; dark idle knob and channel-accent active knob; small filled OUT contact and larger recessed IN with a center pin when linked.
- **Efficient pointer interaction**: one `requestAnimationFrame` render request per pointer frame; compute nearest magnetic candidate only during drag; the received graph changes on accepted release, never on pointermove. Cancel/invalid-drop do not rewrite the graph or source.
- **Smooth legible wires**: symmetric cubic Bézier path toward left or right, solid translucent drag preview, brighter committed cables; no dashed preview.
- **Operator workflow**: drag OUT to IN, or select source and click IN; direct MAIN, Bus-only serial and parallel Bus paths remain different explicit actions. Do not auto-add a direct master route for a Bus-only path.
- **Presentation**: retained pre-existing global physical MUTE/SOLO button material and channel colors; avoided native GUI/engine edits.
- **Build/publish hygiene**: copied the *qualified HTML blob* from the PR reference into main's `site/preview-v2/index.html`, with `site/preview-v2/build.json` and `site/preview/candidate.json` in **one atomic Git commit** instead of several potentially conflicting Pages publications. Main builder reads this same file for `/preview/mixer.html`.

## Verification ledger

| Gate | Result |
|---|---|
| Node in-memory DOM/workflow suite | **214/214 PASS** |
| Chromium real pointer drag, leftward send and Bus-only action | **PASS** |
| Socket and gain centers unchanged before vs after connection | **PASS** |
| Magnetic off-center release | **PASS** |
| Invalid drop over fader must not create routing edge | **PASS** |
| Responsive geometry and interaction | **PASS 1366×768 / 1680×900 / 1920×1080** |
| Dedicated reference CI | [PASS #41384650297](https://github.com/masarray/arsonmix/actions/runs/41384650297) |
| Main Pages publication commit | [`b42391f`](https://github.com/masarray/arsonmix/commit/b42391fb96d4e322db73929fedb34c2f3b5dc099) |
| Production main UI smoke and automatic Pages deployment | Follow release gate status in GitHub Actions, do not infer from source commit |

## Remaining audit findings / separation boundaries

1. **P0: native routing canonical graph is still separate.** The HTML's wire links are DEMO state. Do not claim ASIO or VST3 sound exists until `GraphIntent → validated audio graph → GraphAccepted(revision)` is confirmed with DSP and device-epoch tests.
2. **P0: legacy SEND slots and Sends-on-Faders are not the same model as wire edges.** In native integration, migrate/reconcile those graph paths and prohibit hidden audible duplicate Sends before promoting this UX to production.
3. **P1: upper physical I/O has overlapping responsibilities with wire routing.** Native UI should show verified physical IN and a compact read-only OUT/route summary; reserve internal graph edits for WIRE, using native signal data.
4. **P1: cross-bank navigator and keyboard-first accessibility.** Current clicking source across banks is preserved; automatic drag across changing banks is not certified.
5. **P1: screen reader, touch/precision and compositor benchmarking.** DOM + Chromium coverage does not substitute for Windows high-DPI screenshot review, hardware tactile judgment or actual audio load/latency tests.
6. **P2: source cleanup.** HTML reference remains large and contains historical V12–V16 fallback CSS. Consolidate only after recorded three-width pixel baselines, avoiding risky wholesale CSS surgery during publication.

## Test and release integrity

- Production source remains PR #271 renderer and PR #266 audio-engine owner; do not merge PR #278 by default.
- GitHub Pages is a **preview-only operator workflow reference**, explicitly says `SIMULATED METERS / NO REAL AUDIO`.
- All new preview data is versioned and traceable to the isolated PR source blob, test run and atomic main publication commit.
- Test changes are confined to the focused `reference-wire-qa.yml`; no duplicate heavyweight Windows/ASIO pipeline for every PR draft change.
