# ArSon Harmony — Manual Key + Music Track (2026-11-08)

## Accepted workflow

1. Put instrumental/backing music on a different INPUT track (for example CH02); singer microphone on CH05.
2. Insert ArSon Harmony into the singer's existing native FX rack. Key Source defaults to Manual Key. Select note and Major/Minor using the original wheel.
3. Optionally choose Music Track → CH02. Harmony samples the *actual* post-FX/pre-fader CH02 PCM, using the same ChannelId FX callback. This is read-only and does not wire/mix instrumental audio into the singer.
4. Only after sufficiently stable tonal evidence does the native DSP follow the detected key and scale. Before a confident estimate, on ambiguity, source silence/disconnection or source deletion, Harmony uses the saved Manual Key.
5. Clicking a note or scale returns to Manual Key immediately. Presets and mixer session preserve both the manual fallback and music source selection.

## Safe engine integration

- HarmonyMusicKey.h: fixed-size 36-bin chroma analysis at approximately 6kHz, prepared resonator coefficients, bounded analysis per block, conservative temporal evidence, 1.5s uncertainty expiry, 1s silence reset. No callback heap, locks, trigonometry, FFT scratch allocation or worker thread.
- DynamicChannelFxLeaseBank.h: precomputes exact pointers to Harmony instances off realtime. The accepted native FX callback publishes a post-FX/pre-fader PCM guide from a different accepted ChannelId (INPUT/GROUP), whether or not the source channel has inserts. No extra PCM routing authority or audible send.
- Main.cpp: requires approved decimal ChannelId, a non-self native INPUT/GROUP in the accepted graph, and matching instance/epoch/revision. GUI controls are SPSC queued, audio-applied, ACKed and read back.
- ChannelHarmonyProcessor.h: version-2 state stores guideMode and guideChannelId plus the existing manual key/scale; older version-1 state restores as Manual. Missing live music reverts to Manual. Existing wet/dry PDC unchanged.
- Editor: a compact Key Source selector and estimated-key indicator in the *one approved* /preview/ UI; JavaScript never computes or pretends to detect sound.

## Limitations

This is musical-key estimation, not chord recognition or a substitute for singer listening: relative minor/major and sparse/key-changing pieces can remain ambiguous. No claim of live chord-aware harmonization, formant-preserving synthesis, or smaller latency. Manual Key is the recommended live fail-safe. The original granular Harmony shifter still reports approximately 32ms latency at 48kHz before device round-trip.

## Qualification

- Portable native C++ test: C-major/A-minor synthesized accompaniment at 48/96kHz, silence expiry, manual fallback, invalid selection.
- JUCE native bank PCM test: actual cross-channel tap into the real accepted Harmony processor, signal not altered, missing source timeout, v2 state roundtrip and v1 state migration.
- UI single-approved-GUI and ACK contracts, then selective exact-HEAD Windows CTest and Chromium screenshots.
- Physical ASIO singing and real music/song key accuracy are still pending until independently verified with an interface and recorded material.
