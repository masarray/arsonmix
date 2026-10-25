# P2.3 — True VU meters, real PAN and fast VST3 bypass

## Changes
- **Native VU meters**: removed the conflicting sine-wave demo meter requestAnimationFrame path in the native WebView. Only Channel 1 input and real master output are driven from JUCE status samples. Other 11 channels remain explicitly inactive visual previews, with meter lamps disabled.
- **Meter appearance**: UI-only peak attack with a small falling-ballistic smoothing on incoming native input and master peak readings (12 Hz status cadence). No fabricated noise or sine animation.
- **Native PAN**: Channel 1 pan slider -100..100 now controls an atomic normalized stereo pan matrix applied *after* actual VST3 processing, before master gain. At centre, stereo passes unchanged. At a hard side, both stereo channels fold into the target channel at 0.5 each; dual-mono does not double its amplitude. Stereo balance is not a full multichannel spatial panner.
- **Fast Studio One-style insert enable**: Every native FX insert on the MIX channel strip now includes a dedicated ON/OFF power toggle. Clicking the plugin body still opens the VST3 editor. Power updates the existing atomic bypass flag, without callback detachment or FX Rack navigation. The FX Rack includes the same on/off state.
- **Logger repaired**: Uploaded 08:48–08:54 diagnostic log contained 364 AUDIO_HEALTH lines with exactly 511 bytes of text each, so time counters and VST slot details were truncated. Queue slot increased from 512 to 4096 bytes and truncation counts are exposed for verification. This does *not* retroactively recover the missing metrics.
- **Latest GUI**: adopted the current style sheet from the metallic PAN/fader GUI branch. No redesign of accepted user artwork.

## Acceptance tests
1. Run the new native EXE and confirm only Channel 1 input and master output react to actual input. All other strip meters remain dormant, even after changing tabs and moving faders.
2. Move Channel 1 PAN to hard left, centre and hard right with stereo headphones. A dual-mono vocal should move to the chosen ear and return to both at centre.
3. Add ArSonKuPik to MIX Channel 1, click the small ⏻ button on its insert (not the editor name). Sound must alternate FX processed/bypassed without an audio device restart. Watch ON/OFF immediately reflect native chain state.
4. Click the plugin name to open the native VST3 editor.
5. Open SIGNAL CHECK and OPEN DIAGNOSTIC LOG, reproduce for 1–2 minutes, confirm AUDIO_HEALTH JSON records are no longer clipped at 511 characters and the callback/plugin timings can be parsed.
6. Verify monitoring still flows automatically and no sonic dropout is introduced.

## Boundaries
- Only Channel 1 is native. The other channels and their mock inserts are visual previews.
- A successful CI build does not prove audio quality or round-trip stability on specific Windows hardware.
- Real ASIO/WASAPI channel assignment for 12 native channels, presets and streaming output is still out of scope.
