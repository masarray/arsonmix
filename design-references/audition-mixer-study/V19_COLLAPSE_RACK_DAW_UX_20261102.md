# ArSonMix — Compact Rack V19: Routing Awareness Without Cable Clutter

Date: 2026-11-02. Scope: PR #278 browser GUI design reference. Native audio remains unintegrated.

## Pattern benchmarks

- FL Studio manual, [Mixer Explained](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm): selected-channel routing emphasizes direction and muted incoming-source send controls; routing cables can be made visible in Mixer View, not globally on every strip.
- FL Studio manual, [Mixer Menu](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_mixermenu.htm): "Routing cables" is an optional visualization.
- Steinberg Cubase 15 [MixConsole](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/mixconsole/mixconsole_mixconsole_window_r.html): dedicated Routing, Inserts and Sends sections remain separate from the fader section.
- REAPER [User Guide](https://dlx.reaper.fm/userguide/): allows sending information in the track control panel when room permits; a Routing Matrix is available for complete graph troubleshooting.

These are **design inspiration**, not claims that those DAWs use ArSonMix's exact numerical badges or compact layout.

## Final collapse contract

1. **No cable paths** when WIRE is collapsed. Keep existing `display:none` SVG guard to preserve the 23px WIRE rack and prevent hidden pointer hit areas.
2. **Show a meaningful folded state** inside each channel's 23px section: `← N` incoming internal paths, `→ N` outgoing internal paths; tiny neutral em dash when none. Outgoing uses the channel's color, incoming is deliberately more subdued. Status is a **summary**, not a separate editable routing surface.
3. Count enabled GUI wire edges, Split-source links, active legacy Send slots, and direct bus/FX primary paths that are not already represented as wire edges. **Do not count the ordinary direct Master path** in the WIRE tally; it remains an I/O concern. This avoids a noisy `→1` on every default track.
4. Clicking the folded summary opens WIRE using the existing rail disclosure handler. The indicator is an accessible labeled button and never causes an unvalidated graph mutation.
5. **28px left section rail**: short `I/O`, `FX`, `W` labels, tiny 3px chevrons (closed = right, open = down), a restrained 2px left activity edge. Remove pseudo-text `OPEN`, `PATCH` and `WIRE` that competed for space.
6. Preserve approved V20 MUTE/SOLO key material, fixed left rail width, 23px collapse, FX rack height ownership, PAN and fader heights. No blur or animation.

## Verification

- 225/225 DOM/workflow tests pass, including legacy Send visibility, per-channel in/out counts and click-to-expand.
- Chromium UI checks at 1366×768, 1680×900, 1920×1080 passed in [GitHub Actions #41397350090](https://github.com/masarray/arsonmix/actions/runs/41397350090).
- V15 and V18 real pointer routing regression tests also passed in the same workflow.
- Qualified atomically into main preview commit [`4403d9d`](https://github.com/masarray/arsonmix/commit/4403d9d255e3a13357600456e7d581802729d389) without modifying native audio sources.

## Related follow-on

A dedicated **Focus** view can later show bright selected-channel outgoing wires above faint incoming wires when expanded. Collapsed WIRE should continue using ONLY this 23px static summary, even if Focus has been enabled for the expanded view. Any change of actual primary path, legacy Sends or graph-edge admission still requires the native audio engine ACK and safe migration strategy.
