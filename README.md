> **ONLY APPROVED ARSONMIX GUI (owner decision, 2026-11-04):** https://masarray.github.io/arsonmix/preview/ . This is the *one* public GUI and Windows UI target, with its specific approved PNG hardware buttons. Bare `/preview-v2/` is wrong as a product preview and is redirect-only; `/preview-buttons/` and old archived previews are also redirect-only. The native JUCE engine must be stitched into exactly the assembled `/preview/` renderer. No alternative `web/index.html`, `web/mixer-layout.js`, `web/console-native.html` or older PR #278/#271 GUI may ship. [Root agent lock](AGENTS.md) · [GUI→native P0 #389](https://github.com/masarray/arsonmix/issues/389) · [current handoff](docs/workstreams/HANDOFF_APPROVED_PREVIEW_V2_NATIVE_20261104.md) (historical filename) · [ENGINE #266](https://github.com/masarray/arsonmix/pull/266) · [FAT #270](https://github.com/masarray/arsonmix/pull/270). Browser GUI is simulation, not proof of PCM.

# ArSonMix — Native Realtime Live Mixer and VST3 Host

A Windows-first **live performance digital mixing console** for stage, karaoke, podcast/radio and flexible physical/virtual output, using the *console workflows* of Studio One / Adobe Audition and *live banking/routing* concepts of Waves eMotion LV1. Not a timeline/DAW audio editor, recorder or OBS-only utility.

## Engineering references (historical designs are not current agent authority)
- [Approved PR #278 V8 GUI design](https://github.com/masarray/arsonmix/pull/278) and [isolated study](https://github.com/masarray/arsonmix/blob/design/audition-mixer-reference-isolated-20261101/design-references/audition-mixer-study/index.html) — visual reference only
- [PRD v5.2 — Operator-first live mixer](docs/PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md)
- [ARD v5.2 — Native Console and realtime safety](docs/ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md)
- [Puzzle Socket v8 — one native source ABI](docs/PUZZLE_INTEGRATION_CONTRACT_V8_20261101.md)
- **Current agent authority:** [AGENTS.md](AGENTS.md) and [Smart CI runbook](docs/SMART_CI_RUNNER_WORKFLOW.md). Historical [ENGINE](docs/workstreams/ENGINE_PUZZLE_V8.md), [GUI](docs/workstreams/GUI_PUZZLE_V8.md), and [FAT](docs/workstreams/FAT_PUZZLE_V8.md) packets are references, not approval gates.
- [AGENTS.md](AGENTS.md) is mandatory for active contributors. Prior G1/G2/R5/L2 dated plans remain in Git history, not current execution authority.

## Historical verified M4 audio boundary (2026-11-01 snapshot; NOT a claim of current universal release)
`main@bb6dff6b42158ba2db4666e0def54bb89abda4f8`: one primary JUCE callback processes **fixed 10 input channels + four AUX**, native MAIN/MONITOR and occupied legacy CH1 VST3/Orbit special path. Windows CI [41263965810](https://github.com/masarray/arsonmix/actions/runs/41263965810) PASS and Chromium [41263965813](https://github.com/masarray/arsonmix/actions/runs/41263965813) PASS; Windows FAT artifact available, **not hardware certified**. Engine [PR #266](https://github.com/masarray/arsonmix/pull/266) is an offline universal publisher/VST lease preparation, NOT yet the live callback cutover. Independent dynamic CH11, Group and FX Return **not shipped**. Zero-FX acoustic crackle [#15](https://github.com/masarray/arsonmix/issues/15) unresolved.

### Product completion means audibly real, not just a fancy UI
Choose device once, then directly from the console:
`interface IN1 → CH07 own native VST3` and `interface IN2 → CH02 own native VST3` → `real Vocal GROUP → MAIN to user-chosen output`, plus `parallel wet FX RETURN`, independent `MONITOR`, real `+CH11`, Show round-trip, stable faders, true native LEDs. No compulsory routing wizard/advanced matrix or manual multi-step STAGE/MUTE/APPLY/START for routine mixing.

## Agent onboarding and active integration — single source of truth

**Every new ChatGPT thread with GitHub connector access must first read
[AGENTS.md](AGENTS.md) and
[Smart CI Runner Workflow](docs/SMART_CI_RUNNER_WORKFLOW.md).**
Treat live GitHub `main`, open PRs and their current exact HEAD as authority,
not stale PR labels or past-thread summaries. At the 2026-11-06 checkpoint
[PR #418](https://github.com/masarray/arsonmix/pull/418) is the canonical P0
DAW direct-input integration candidate; **verify its latest status before
continuing**. Earlier #266/#278/#271/#270 are historical inputs, not mandatory
parallel-agent approval gates.

The ChatGPT agent **works in GitHub itself**: reconcile current code,
continue one appropriate PR, run targeted checks, and choose meaningful
milestone CI via a single owner-authenticated PR comment using the connector:
`/arsonmix-ci ui <SHA>`, `/arsonmix-ci native <SHA>`, or
`/arsonmix-ci fat <SHA>`. The default-branch dispatcher launches the selected
workflow; no manual GitHub Actions button is expected from the owner.
Every relevant PR commit gets **cheap Linux fast contracts**; Chromium,
Windows MSVC/CTest and experimental ZIP are reserved for selected milestones
(and relevant `main` qualification), **not every commit**. `fat` includes
`native` build/CTest, so never request both for the same SHA without a
specific new reason. Avoid duplicate/obsolete PRs, cancelled runner churn,
repeated packaging, hidden status assumptions, and unproven audio claims.

Preserve the sole approved `/preview/`, real accepted ChannelId/EdgeId
routing, one realtime callback, genuine VST3 host, live readback and safe
device control. Physical ASIO sound/crackle evidence remains a separate
stop-ship criterion even when CI is green. See [issue #15](https://github.com/masarray/arsonmix/issues/15).

One accepted ChannelId graph, one JUCE realtime callback, one native VST3 owner, and one request/ACK truth. Continuous controls must not restart the audio device; structural edits prepare safely off callback. Accepted native capabilities gate UI: no fake channel/FX/PCM. Preserve operator-selected I/O and report actual buffer/latency behavior.

## Browser preview, native builds and QA
**ONLY approved GUI:** https://masarray.github.io/arsonmix/preview/ (visual UI reference, **NO REAL AUDIO**). Old `/preview-v2/`, `/preview-buttons/` and `/preview/mixer.html` redirect to this one approved screen. Source: https://github.com/masarray/arsonmix. Windows x64 native packages: [Windows Actions workflow](https://github.com/masarray/arsonmix/actions/workflows/p1-windows.yml). Download only a successful exact-source-SHA artifact; verify `BUILD_PROVENANCE.json` and EXE SHA-256, install WebView2/real device driver, test at safe headphone volume.

Use owner CSS `web/styles-src/**` and deterministic `scripts/build_ui_css.py` regeneration; avoid manual generated HTML drift. Read the current `AGENTS.md` and smart CI runbook, run targeted tests, then trigger only the required Windows/CTest, Chromium or FAT lane **from the connected GitHub connector** at meaningful checkpoints. For physical clicks use SIGNAL CHECK → MARK CRACKLE → COPY FULL REPORT. Real-device 0FX and loaded-FX performance must be signed off before release.

## Licenses and distribution
Respect source/SDK licenses. Third-party VST3 files remain in user-controlled private Studio vault; do not bundle unlicensed commercial plugins. FactoryFX requires signed/provenance-approved host-qualified packages. Reference workflows, never copy proprietary Audition/Studio One/LV1 artwork.

## Historical diagnostics and visual references (NOT active execution authority)

Retained for traceable physical hardware diagnostics under [#15](https://github.com/masarray/arsonmix/issues/15):
- [Smart Audio Engine ARD — archival render/device-clock analysis](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md).
- [Smart Audio Engine Implementation Blueprint — archival ASIO/WASAPI FAT details](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md).
- [Original approved GUI baseline — archival visual comparison](docs/GUI_BASELINE_LOCK_2026-10-25.md).

These explain prior device-clock evidence and fader/LED design. For **current implementation and workflow authority**, read `AGENTS.md`, the live GitHub code/PR state, and the Smart CI runbook first. Earlier PRD/ARD versions are historical design references, not current operational instructions. The historical specs do not override native ChannelId admission or justify an extra render callback.
