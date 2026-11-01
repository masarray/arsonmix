# ArSonMix — Native Realtime Live Mixer and VST3 Host

A Windows-first **live performance digital mixing console** for stage, karaoke, podcast/radio and flexible physical/virtual output, using the *console workflows* of Studio One / Adobe Audition and *live banking/routing* concepts of Waves eMotion LV1. Not a timeline/DAW audio editor, recorder or OBS-only utility.

## One source of truth
- [PRD v5 — Operator-first product](docs/PRD_ARSONMIX_OPERATOR_FIRST_LIVE_MIXER_V5_20261101.md)
- [ARD v5 — Universal ChannelId architecture](docs/ARD_ARSONMIX_UNIVERSAL_REALTIME_CONSOLE_V5_20261101.md)
- [EXECUTION LOCK v6 — one delivery path, parallel ownership, exact acceptance](docs/EXECUTION_LOCK_UNIVERSAL_LIVE_CONSOLE_V6_20261101.md)
- [ENGINE workstream](docs/workstreams/ENGINE_UNIVERSAL_CONSOLE_V6.md) · [GUI workstream](docs/workstreams/GUI_OPERATOR_CONSOLE_V6.md) · [INTEGRATION/FAT workstream](docs/workstreams/INTEGRATION_FAT_CONSOLE_V6.md)
- [AGENTS.md](AGENTS.md) is mandatory for active contributors. Prior G1/G2/R5/L2 dated plans remain in Git history, not current execution authority.

## Verified actual status (2026-11-01 snapshot)
`main@bb6dff6b42158ba2db4666e0def54bb89abda4f8`: one primary JUCE callback processes **fixed 10 input channels + four AUX**, native MAIN/MONITOR and occupied legacy CH1 VST3/Orbit special path. Windows CI [41263965810](https://github.com/masarray/arsonmix/actions/runs/41263965810) PASS and Chromium [41263965813](https://github.com/masarray/arsonmix/actions/runs/41263965813) PASS; Windows FAT artifact available, **not hardware certified**. Engine [PR #266](https://github.com/masarray/arsonmix/pull/266) is an offline universal publisher/VST lease preparation, NOT yet the live callback cutover. Independent dynamic CH11, Group and FX Return **not shipped**. Zero-FX acoustic crackle [#15](https://github.com/masarray/arsonmix/issues/15) unresolved.

### Product completion means audibly real, not just a fancy UI
Choose device once, then directly from the console:
`interface IN1 → CH07 own native VST3` and `interface IN2 → CH02 own native VST3` → `real Vocal GROUP → MAIN to user-chosen output`, plus `parallel wet FX RETURN`, independent `MONITOR`, real `+CH11`, Show round-trip, stable faders, true native LEDs. No compulsory routing wizard/advanced matrix or manual multi-step STAGE/MUTE/APPLY/START for routine mixing.

## Active work and GitHub ownership
- Native publisher/full-fidelity callback/CH1 FX migration: [draft PR #266](https://github.com/masarray/arsonmix/pull/266).
- New compact single-screen, native-state-driven console: [GUI issue #268](https://github.com/masarray/arsonmix/issues/268), branch `work/operator-console-v6`.
- Unified product acceptance/reconciliation: [epic #192](https://github.com/masarray/arsonmix/issues/192).
- Physical sound/crackle blocker: [#15](https://github.com/masarray/arsonmix/issues/15).
- Execution orchestration document PR: [#267](https://github.com/masarray/arsonmix/pull/267).

One accepted stable-ChannelId graph, one JUCE realtime callback, one native VST3 owner, one request/ACK truth. Continuous controls do not trigger device restart; structural edits prepare safely off callback. Native accepted capability gates UI; **no fake CH11/FX/PCM**. Operator-selected legal I/O only; correct device buffering and plugin latency disclosures, not '0ms' guarantees.

## Browser preview, native builds and QA
Browser preview: https://masarray.github.io/arsonmix/preview/mixer.html (UI demo **not real DSP proof**). Source: https://github.com/masarray/arsonmix. Windows x64 native packages: [Windows Actions workflow](https://github.com/masarray/arsonmix/actions/workflows/p1-windows.yml). Download only a successful exact-source-SHA artifact; verify `BUILD_PROVENANCE.json` and EXE SHA-256, install WebView2/real device driver, test at safe headphone volume.

Use owner CSS `web/styles-src/**` and deterministic `scripts/build_ui_css.py` regeneration; avoid manual generated HTML drift. Read full failing CI logs, run targeted tests, then one Windows/CTest and Chromium gate at meaningful vertical checkpoints. For physical clicks use SIGNAL CHECK → MARK CRACKLE → COPY FULL REPORT. Real-device 0FX and loaded-FX performance must be signed off before release.

## Licenses and distribution
Respect source/SDK licenses. Third-party VST3 files remain in user-controlled private Studio vault; do not bundle unlicensed commercial plugins. FactoryFX requires signed/provenance-approved host-qualified packages. Reference workflows, never copy proprietary Audition/Studio One/LV1 artwork.

## Historical diagnostics and visual references (NOT active execution authority)

Retained for traceable physical hardware diagnostics under [#15](https://github.com/masarray/arsonmix/issues/15):
- [Smart Audio Engine ARD — archival render/device-clock analysis](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md).
- [Smart Audio Engine Implementation Blueprint — archival ASIO/WASAPI FAT details](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md).
- [Original approved GUI baseline — archival visual comparison](docs/GUI_BASELINE_LOCK_2026-10-25.md).

These explain prior device-clock evidence and fader/LED design. For **current feature architecture, priority and execution**, the PRD/ARD v5 and execution lock v6 at the top of this document take precedence. The historical specs do not override native ChannelId admission or justify an extra render callback.
