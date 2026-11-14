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

## Real Windows candidate gate correction — strict native state bytes

The first full Windows build successfully compiled the actual
`ARMixConsole` plus P8.3 JUCE test executable, and 7/8 focused tests
passed. The 8th revealed a real bug: JUCE `readByte()` can return
signed -1 for corrupt `0xFF`, so a `>1` boolean check accidentally
accepted it. The v4 and inherited v1-v3 boolean admissions now
require **exactly 0 or 1**. A dedicated logical/unpublished editor
test also prevents queued mode edits from awaiting a nonexistent audio
callback, while published realtime edits require the full 12 ms
fade-and-audio ACK transaction. State snapshots are now blocked by an
atomic pending-mode fence during the transition. This corrected
candidate must pass all 8 Windows CTests before packaging.

## CRITICAL: USER HEADPHONE FAT REJECTED, 2026-11-15 WIB

The owner tested the full native mixer P8.3 portable candidate from run
43635510105 and found the sound **compressed/boxed-in, quiet and lacking
body**, far worse than the approved P8.2. Treat the physical headphone
result as a HARD FAILURE, not a request for additive gain or more reverb.
The P8.3 portable artifact is preserved for forensic reproduction
ONLY. **Do not recommend it as an accepted listening candidate**.

Source-level root causes supported by current code:
1. `transparentBass` is disabled whenever `positionedMode` is true.
   That removes P8.2's original L/R LF pressure restoration.
2. `musicSafe` and `sideRescue` are disabled for positioned mode.
   That removes P8.2 stereo SIDE musical preservation and centered
   anti-comb filtering.
3. `roomEnabled&&!positionedMode` excludes P8.3 from P8.2 scene
   presence calibration, despite still using the scene's FIR, ER/FDN,
   and output gain/smoothing paths.
4. Two measured HRIRs at +/-30 degrees are valid directional research
   sources but do NOT alone guarantee consistent full-spectrum music;
   their MID/SIDE transfer can have low-energy and cancellation valleys.
5. The "LF480" substitute only conditions the direct 480-Hz low band,
   uses an intentionally non-measured 2x2 matrix, and does not guarantee
   stereo music above its crossover. Prior 4-bin +/-6dB acceptance is
   inadequate for real music.

Existing exact native 48k PCM evidence: anti-phase P8.2 energy
8.967784318, physical speaker 1.520875549 (-7.71dB), opt-in LF480
5.489325122 (-2.13dB). LF480 at 461Hz is -5.801dB and correlated
mono at 1703Hz -3.887dB relative to P8.2. These do not by themselves
prove the exact frequency profile of the user's song/headphones, but
they INVALIDATE the previous "music fidelity PASS" conclusion.

FAT gate is now deliberately strict: LF480 must preserve five
program-material fixtures within +/-3dB full-band level and both mono
and anti-phase spectral probes at 83/461/1703/5387Hz within +/-2.5dB
at both 44.1/48k. Any violation MUST FAIL the CI, preventing another
"green" portable candidate that is materially quieter or hollow.
Additional continuous-spectrum FFT, transient, binaural IACC,
loudness-matched headphone and human sound-location tests remain
mandatory. Do not game these gates with auto-gain.
