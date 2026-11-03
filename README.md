> **PRIMARY ARSONMIX GUI AUTHORITY — PR #278 V8 (2 November 2026).** The approved operator experience is [PR #278's Audition-style single Mixer Console](https://github.com/masarray/arsonmix/pull/278), with Studio One fast workflows and the latest [V8 adaptive geometry rules](https://github.com/masarray/arsonmix/blob/design/audition-mixer-reference-isolated-20261101/design-references/audition-mixer-study/V8_ADAPTIVE_BANK_COLLAPSE_ACCEPTANCE_20261102.md). It supersedes competing GUI concepts, legacy multi-workspace/default Audio Setup navigation and outdated v6/v7 operator-layout prescriptions. **Production owner:** [GUI #271](https://github.com/masarray/arsonmix/pull/271) maps this design to the actual native WebView. **Audio owner:** [ENGINE #266](https://github.com/masarray/arsonmix/pull/266) provides one JUCE callback and true PCM/ChannelId/VST3. **Release owner:** [FAT #270](https://github.com/masarray/arsonmix/pull/270) enforces one same-source integration gate in [epic #192](https://github.com/masarray/arsonmix/issues/192).

> **Current normative product + architecture:** [PRD v5.2 §9](docs/PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md#9-normative-v52-product-design-lock--user-approved-pr-278-mixer-workflow) · [ARD v5.2 §K](docs/ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md#k-normative-v52-design-to-native-execution-contract--pr-278-reference) · [Puzzle Socket v8](docs/PUZZLE_INTEGRATION_CONTRACT_V8_20261101.md). Earlier v6/v7 release-train and design documents are historical references, **not alternative active GUI authority**. The PR #278 HTML is a **SIMULATED VISUAL REFERENCE, NOT NATIVE AUDIO**; never merge its fake meters/plugin catalog/PCM into production. No partial EXE customer UAT, no claim of shipped universal ChannelId callback until one integrated exact-SHA Windows native + Chromium + hardware qualification passes.

# ArSonMix — Native Realtime Live Mixer and VST3 Host

A Windows-first **live performance digital mixing console** for stage, karaoke, podcast/radio and flexible physical/virtual output, using the *console workflows* of Studio One / Adobe Audition and *live banking/routing* concepts of Waves eMotion LV1. Not a timeline/DAW audio editor, recorder or OBS-only utility.

## One source of truth
- [Approved PR #278 V8 GUI design](https://github.com/masarray/arsonmix/pull/278) and [isolated study](https://github.com/masarray/arsonmix/blob/design/audition-mixer-reference-isolated-20261101/design-references/audition-mixer-study/index.html) — visual reference only
- [PRD v5.2 — Operator-first live mixer](docs/PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md)
- [ARD v5.2 — Native Console and realtime safety](docs/ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md)
- [Puzzle Socket v8 — one native source ABI](docs/PUZZLE_INTEGRATION_CONTRACT_V8_20261101.md)
- [Current owner packets: ENGINE](docs/workstreams/ENGINE_PUZZLE_V8.md) · [GUI](docs/workstreams/GUI_PUZZLE_V8.md) · [INTEGRATION/FAT](docs/workstreams/FAT_PUZZLE_V8.md)
- [AGENTS.md](AGENTS.md) is mandatory for active contributors. Prior G1/G2/R5/L2 dated plans remain in Git history, not current execution authority.

## Historical verified M4 audio boundary (2026-11-01 snapshot; NOT a claim of current universal release)
`main@bb6dff6b42158ba2db4666e0def54bb89abda4f8`: one primary JUCE callback processes **fixed 10 input channels + four AUX**, native MAIN/MONITOR and occupied legacy CH1 VST3/Orbit special path. Windows CI [41263965810](https://github.com/masarray/arsonmix/actions/runs/41263965810) PASS and Chromium [41263965813](https://github.com/masarray/arsonmix/actions/runs/41263965813) PASS; Windows FAT artifact available, **not hardware certified**. Engine [PR #266](https://github.com/masarray/arsonmix/pull/266) is an offline universal publisher/VST lease preparation, NOT yet the live callback cutover. Independent dynamic CH11, Group and FX Return **not shipped**. Zero-FX acoustic crackle [#15](https://github.com/masarray/arsonmix/issues/15) unresolved.

### Product completion means audibly real, not just a fancy UI
Choose device once, then directly from the console:
`interface IN1 → CH07 own native VST3` and `interface IN2 → CH02 own native VST3` → `real Vocal GROUP → MAIN to user-chosen output`, plus `parallel wet FX RETURN`, independent `MONITOR`, real `+CH11`, Show round-trip, stable faders, true native LEDs. No compulsory routing wizard/advanced matrix or manual multi-step STAGE/MUTE/APPLY/START for routine mixing.

## Active work and GitHub ownership
- Native publisher/full-fidelity callback/CH1 FX migration: [draft PR #266](https://github.com/masarray/arsonmix/pull/266).
- Approved GUI/UX visual authority: [PR #278](https://github.com/masarray/arsonmix/pull/278). Its browser demo is not native audio; [GUI #271](https://github.com/masarray/arsonmix/pull/271) owns the real native-state-driven production adaptation.
- Unified product acceptance/reconciliation: [epic #192](https://github.com/masarray/arsonmix/issues/192).
- Physical sound/crackle blocker: [#15](https://github.com/masarray/arsonmix/issues/15).
- Product integration owner: [FAT #270](https://github.com/masarray/arsonmix/pull/270), enforcing exact SHA + PR #278 visual authority without replacing DSP.

One accepted stable-ChannelId graph, one JUCE realtime callback, one native VST3 owner, one request/ACK truth. Continuous controls do not trigger device restart; structural edits prepare safely off callback. Native accepted capability gates UI; **no fake CH11/FX/PCM**. Operator-selected legal I/O only; correct device buffering and plugin latency disclosures, not '0ms' guarantees.

## Browser preview, native builds and QA
**Primary approved PR #278 V8 GUI study preview:** https://masarray.github.io/arsonmix/preview/mixer.html (visual reference / simulated meters, **NO REAL AUDIO**). Alternate preview route `/preview-v2/` points to the same isolated design study; it is not a second release GUI. Source: https://github.com/masarray/arsonmix. Windows x64 native packages: [Windows Actions workflow](https://github.com/masarray/arsonmix/actions/workflows/p1-windows.yml). Download only a successful exact-source-SHA artifact; verify `BUILD_PROVENANCE.json` and EXE SHA-256, install WebView2/real device driver, test at safe headphone volume.

Use owner CSS `web/styles-src/**` and deterministic `scripts/build_ui_css.py` regeneration; avoid manual generated HTML drift. Read full failing CI logs, run targeted tests, then one Windows/CTest and Chromium gate at meaningful vertical checkpoints. For physical clicks use SIGNAL CHECK → MARK CRACKLE → COPY FULL REPORT. Real-device 0FX and loaded-FX performance must be signed off before release.

## Licenses and distribution
Respect source/SDK licenses. Third-party VST3 files remain in user-controlled private Studio vault; do not bundle unlicensed commercial plugins. FactoryFX requires signed/provenance-approved host-qualified packages. Reference workflows, never copy proprietary Audition/Studio One/LV1 artwork.

## Historical diagnostics and visual references (NOT active execution authority)

Retained for traceable physical hardware diagnostics under [#15](https://github.com/masarray/arsonmix/issues/15):
- [Smart Audio Engine ARD — archival render/device-clock analysis](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md).
- [Smart Audio Engine Implementation Blueprint — archival ASIO/WASAPI FAT details](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md).
- [Original approved GUI baseline — archival visual comparison](docs/GUI_BASELINE_LOCK_2026-10-25.md).

These explain prior device-clock evidence and fader/LED design. For **current feature architecture, priority and execution**, the PRD/ARD v5 and execution lock v6 at the top of this document take precedence. The historical specs do not override native ChannelId admission or justify an extra render callback.
