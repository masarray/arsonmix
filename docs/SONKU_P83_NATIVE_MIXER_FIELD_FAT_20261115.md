# SonKu P8.3 — Native Windows Mixer Headphone FAT (research only)

**Candidate**: exact full native JUCE AR MIX Studio.exe from the
\`ArSonMix-SonKu-P83-Native-Mixer-Windows-x64-*\` GitHub Actions artifact.
The artifact is a portable research mixer, not a public release or
installer. The build must say P8.3 in \`BUILD_PROVENANCE.json\`, use
\`stateSchema=4\` and cite the source SHA. No dearVR parity claim.

## Run the REAL native mixer, not the offline WAV renderer

1. Extract the whole artifact to its own directory, never overwrite
   your existing P8.2-approved test mixer or public Harmonice install.
   Open the packaged native mixer .exe. Keep the P8.2 golden portable
   for controlled A/B. No ASIO 4–8-channel device is required:
   a working headphone stereo output is sufficient.
2. In **Audio Setup**, select your actual interface ASIO/WASAPI and
   headphone playback L/R; verify audio device channel routing and
   Master Mono is OFF. Use **48k, buffer 128** initially, with a
   safe monitor volume. The existing native JUCE PCM authority remains
   the only audio graph; avoid multiple independently monitoring paths.
3. Route your own stereo music source through a native mixer channel,
   insert **8D-SonKu** in that channel's FX rack. Use the normal
   approved GUI /preview/ and mixer routing, not a second WebAudio tab.
4. Open SonKu. A real P8.3 research binary shows a small **RENDER MODE**
   dropdown after the native v4 snapshot. If no selector appears,
   the running executable is NOT a functioning P8.3 host candidate.
   Do not treat the browser preview as audio.
5. Switch between:
   - **MUSIC BED · P8.2**: last-approved tonal/bass baseline;
   - **SPEAKER MONITOR · PHYSICAL**: exact measured KEMAR two-speaker
     transfer. Opposite-phase bass may cancel physically;
   - **SPEAKER MONITOR · MUSIC LF**: 480Hz explicit compromise,
     not a physically measured low-band BRIR;
   - **MONO OBJECT · LEFT INPUT**: LEFT input only drives a single
     virtual source, useful with one mono test channel.
   A mode edit should first queue, then report audio **APPLIED**
   and return the accepted settings in readback. No fake switch.
6. Set STATIC, position=0° and SPREAD=60° for stereo front
   speakers at -30/+30°, elevation 0°, distance 1.6m. Compare ROOM OFF
   first; then SQUARE with reflections 55%, reverb 30%.
   Head yaw slider simulates head rotation with world-fixed sources.
   It is NOT camera/head-tracking hardware.
7. Listen at matched loudness on the exact same headphone, interface,
   source and mix settings. Test directional RIGHT/LEFT, FRONT/REAR,
   overhead false elevation, real distance and vocals/bass at equal
   levels, first at low monitor volume. Avoid accidentally listening
   to double-played signal routed directly to MAIN and through SonKu.

## Simple report: the hearing verdict matters

Record under each of the three modes: perceived source location,
externalization (inside/outside head), overhead illusion, distance,
vocal timbre, bass/kick fullness, artifacts/clicks, and CPU/XRUN
if the device reports them. For moving sources test 0°, ±30°, ±90°,
±150° and 180°. Perform blind trials at 4 and 8 directions with a
confusion matrix; target ≥80% cardinal correctness before comparison
against dearVR PRO 2 on the same headphones.

**Acceptance guard:** no merge/public release or declaration of
dearVR equivalence until human listening signs off. Generic KEMAR
HRTFs, 480Hz LF directional compromise, limited non-individual
pinna cues and absent automatic head tracking remain limitations.
Computer CI cannot establish real-world externalization or ASIO
hardware p95/p99/XRUN. Retain P8.2 as an always-available fallback.

## CI trust boundary

Only count the zip as the P8.3 native candidate when the pipeline
compiled **ARMixConsole** with
\`ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT=ON\`,
ran \`NativeSonKuP83JuceV4AudioAckLegacyRestoreAndHeadYaw\`,
passed source/legacy regression tests, verified executable PE/GUI
resources, and generated an exact source-commit-provenanced portable
artifact. Unrelated P7.x/P8.0 Windows outputs do not contain P8.3.
