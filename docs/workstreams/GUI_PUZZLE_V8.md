# HISTORICAL GUI PUZZLE V8 — RETIRED AS A GUI IMPLEMENTATION PATH (2026-11-04)

**Do not continue PR #271's legacy `web/mixer-layout.js`/M4 UI renderer as the product.** It is superseded by https://masarray.github.io/arsonmix/preview-v2/, source latest `main/site/preview-v2/**`.

Only one GUI workstream remains: [#389](https://github.com/masarray/arsonmix/issues/389) and [canonical handoff](HANDOFF_APPROVED_PREVIEW_V2_NATIVE_20261104.md). The job is to deterministically bundle the same approved GUI and CSS/JS/assets into JUCE WebView2, then add **only a thin native state/intent adapter**. ENGINE #266 with native WIRE #387 supplies true PCM/ChannelId/VST3, FAT #270 checks actual Windows GUI visual parity and real audio. Do not make independent renderers, extra CSS skins, mock bridges, rebuilt 10CH screens, or a second JS audio engine.

**Retained hard requirements:** `contracts/console-puzzle-v8.json` schemaVersion 6, single actual C++-produced fixture, canonical uint64-string ChannelId/EdgeId, `arMixConsoleSubscribe/Snapshot/Intent/Ack`, later same-revision native accepted readback, stale/reject rollback, verified device pins, fail-closed supportedOps, no fake LIVE, no user UAT until joined exact-SHA release. Earlier S0–S3 descriptions are historical for GUI design but still inform native verification.
