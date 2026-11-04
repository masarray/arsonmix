# ArSonMix — SINGLE APPROVED GUI: /preview/
**BINDING OWNER CORRECTION 2026-11-04 — overrides prior incorrect /preview-v2/ target.**

**ONLY approved/public/product GUI: https://masarray.github.io/arsonmix/preview/**. There must be ONE public preview, ONE actual rendered GUI, and ONE native Windows GUI. **/preview-v2/ is rejected**: do not display, promote, publish or base native design on it as a separate UI. Do not substitute another console from `web/index.html`, `web/mixer-layout.js`, `web/console-native.html`, PR #278 or old M3/V48 mock. **The JUCE ENGINE must stitch into the EXACT approved /preview/ GUI**, never redesign it.

## Multi-agent GitHub coordination — REQUIRED (issue #397)

Before touching ArSonMix code, read [workstream ownership and collaboration contract](docs/workstreams/MULTI_AGENT_COORDINATION.md) and [coordination control tower #397](https://github.com/masarray/arsonmix/issues/397). GitHub main, active PRs, changed-file overlap and accepted native ABI are the shared state; independent ChatGPT threads cannot see each other's private unpushed work.

- **GUI** owns `site/**` and browser presentation. ONLY the assembled `/preview/`, no alternative renderer.
- **ENGINE** owns `engine/**` and realtime DSP/PCM/device/VST3 sources; **#266** remains sole callback producer.
- **INTEGRATOR** alone owns `src/Main.cpp`, `CMakeLists.txt`, `integration/**` and `web/approved-native*` and assembles one Windows application.
- **FAT** owns native Windows evidence, packaging and independent QA; FAT must not edit production DSP or GUI to claim PASS.

New PRs require the owner and linked issue metadata from `.github/PULL_REQUEST_TEMPLATE.md`. Existing Windows and Chromium workflows run the cheap, read-only source-owner/collision preflight; no extra workflow. Cross-owner changes must be handed off to the owner through issue #397, **not** implemented in parallel. Mark superseded older PRs `<!-- arsonmix-coordination: parked -->` while extracting unique verified changes. Pre-policy PRs #395 and earlier receive nonblocking findings while DRAFT; when marked ready_for_review they must meet the SAME strict gate and re-run CI. This is not permission to merge conflicting code. One exact-SHA integrator gates a Windows EXE on real native PCM + visual parity + FAT; missing hardware evidence is PENDING.

## Verified current published GUI implementation
`site/preview/index.html` was merely a wrapper/iframe pointing at a generated `/preview-buttons/index.html`. `scripts/build_pages.py` generates that **approved PNG hardware-button variant** using current `site/preview-v2/index.html` as an INTERNAL HTML/WIRE/FX/fader source, with specifically `site/preview-buttons/assets/mixer-control-keys.css`, `mixer-control-keys.js` and `mute-solo-pfl-56x37.png` as approved button source. **The assembled result behind /preview/ is the visual authority, not /preview-v2/ in isolation.** There must be exactly ONE deterministic assembled output and no independent published variant. Internal source files may survive only as build inputs until safely consolidated; they are NOT alternate product GUIs. **Current approved visual including PNG keys must never be silently replaced by the base variant.**

## Active contract and ownership
- [P0 native GUI stitch #389](https://github.com/masarray/arsonmix/issues/389) — compile the **exact /preview/ assembled GUI** and component assets into JUCE WebView2, no new layout/renderer. Update the issue from earlier incorrect preview-v2 wording.
- GUI reference maintenance: currently reviewed `main` component/source changes. Publish ONLY `/preview/` and no independent `/preview-v2/`, `/preview-buttons/` or historical Vxx GUI. Old URLs may redirect to /preview/ but never contain another mixer UI.
- ENGINE [#266](https://github.com/masarray/arsonmix/pull/266) + [#387](https://github.com/masarray/arsonmix/pull/387) keep ONE primary JUCE realtime callback, ONE accepted ChannelId/EdgeId PCM graph, ONE genuine native VST3 lease/editor; do not ship ENGINE's own `web/console-native.*` visual renderer.
- FAT [#270](https://github.com/masarray/arsonmix/pull/270), native epic #192, hardware crackle #15. One integrated exact-SHA Windows EXE; exact approved generated `/preview/` HTML/assets SHA256 and screenshot/interaction parity; native PCM + VST3 + actual hardware FAT.

## Native architecture and user behavior
- Bundle exactly the SAME generated GUI shown at `/preview/` into native WebView2 using a deterministic current-main source/asset provenance; do not hand-copy old mock HTML or duplicate CSS ownership. Thin native adapter may change **data and event authority only**, not GUI appearance or physical interaction.
- v8 schemaVersion=6 `arMixConsoleSubscribe`, `Snapshot`, `Intent`, `Ack`; canonical uint64 decimal-string ChannelId/EdgeId/device epochs/graph revisions. Intent accepted only after matching ACK and later accepted same-revision native snapshot. PENDING/REJECTED/STALE/unsupported != LIVE.
- Default CH→MAIN is real graph edge. Display WIRE only for the selected channel; focused MAIN fragments only expanded; collapsed wire quiet; OUT lit only for **custom non-MAIN** outgoing edges. Wire, OUT, IN, Matrix, Navigator, Scene/Undo, FX, fader, PAN, meter derive from native accepted graph/control state. Physical device pin mapping only actual verified ASIO/WASAPI ports; no fake meter/audio/FX in Pages.
- Structural graph off callback, atomic immutable publication; zero callback locks/heap/UI/VST scanning/disk. Preserve CH1 vendor VST3/Orbit, original 10CH+4AUX, Show/device paths during migration. Never create duplicate routing PCM or special fake CH1; no JS/WebAudio audio engine.
- One meaningful vertical milestone: actual compiled Windows EXE displays exact /preview/ skin (including its approved buttons), then real native input→channel→MAIN, meter/fader, VST3 and WIRE intent/ACK/snapshot, then full FAT. Verify 1366×768, 1680×900, 1920×1080 and bank/density/fold states.
- `node scripts/check_console_puzzle.mjs --release` must fail on any alternate GUI. No user UAT until a coherent native product; physical rig absent => FAT PENDING, not product ready.

**Current execution:** [single handoff](docs/workstreams/HANDOFF_APPROVED_PREVIEW_V2_NATIVE_20261104.md) is retained as filename for link compatibility but CONTENT MUST point to /preview/ alone. Older v5/v6/v7 and historical PR #278/#271 GUI paths never override this document; audio/safety requirements still apply. **No one may revive /preview-v2/ as a second GUI**.

## Historical diagnostics retained (not GUI authority)
The archival `docs/GUI_BASELINE_LOCK_2026-10-25.md` remains available only for regression forensics and former native asset compatibility. It NEVER outranks the user's approved **/preview/** assembled GUI or its PNG keys.

### Historical physical audio diagnostics (not GUI product authority)
- [2026-10-25 Smart Audio Engine ARD](docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md)
- [Smart Audio Engine Implementation Blueprint](docs/SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md)
- [Dry physical crackle #15](https://github.com/masarray/arsonmix/issues/15)
These remain for device-clock, render-discontinuity and hardware FAT engineering only. Their old GUI v0.8 descriptions do not override the sole **/preview/** GUI.
