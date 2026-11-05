# ArSonMix — SINGLE APPROVED GUI: /preview/
**BINDING OWNER CORRECTION 2026-11-04 — overrides prior incorrect /preview-v2/ target.**

**ONLY approved/public/product GUI: https://masarray.github.io/arsonmix/preview/**. There must be ONE public preview, ONE actual rendered GUI, and ONE native Windows GUI. **/preview-v2/ is rejected**: do not display, promote, publish or base native design on it as a separate UI. Do not substitute another console from `web/index.html`, `web/mixer-layout.js`, `web/console-native.html`, PR #278 or old M3/V48 mock. **The JUCE ENGINE must stitch into the EXACT approved /preview/ GUI**, never redesign it.

## One integration owner — binding product recovery (2026-11-05)

Previous four-lane ENGINE / INTEGRATOR / FAT / GUI ownership and active-PR
collision gates are **superseded**. A single product integration PR may edit
engine, native host, GUI data bridge, tests, packaging and CI as necessary.
Declare `<!-- arsonmix-integration: single-owner -->` in its PR body so the
existing lightweight qualification selector skips cross-thread admission,
not the actual Windows build, JUCE/CTest, browser gates or release checks.
No waiting for abandoned thread approvals; no fake milestone/progress claims.

Before a change: fetch latest main HEAD, compare merged #266/#408/#406,
reconcile source file-by-file, and preserve functional VST3, Show, device
selection and the accepted GUI. Use **one integrated PR** with meaningful
product changes. Branch protection, review, exact-SHA tests, source integrity
and independently witnessed hardware FAT remain binding. No forced merge,
no uncontrolled callbacks, no silent auto-arm or output reroute. If real
hardware is unavailable, mark physical audio FAT **PENDING**.

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
