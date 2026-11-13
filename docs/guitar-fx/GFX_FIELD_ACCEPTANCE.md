# Guitar FX Rig — first physical Hi-Z/ASIO field qualification
**Build:** PR #607 design-native M1 candidate. This is **not** a public release, not NAM/neural modeling and not measured cabinet captures. Never confuse a green GitHub Windows FAT workflow with a guitar/audio hardware FAT.

## Setup
- Obtain only the exact-SHA, independently verified Windows FAT ZIP artifact linked in PR #607. Check provenance: expected PR HEAD SHA, validated asset bundle and CTest before executing.
- Use the normal ArSonMix native application. No second Guitar FX executable. Open the approved mixer UI (its existing /preview/ chrome), not a standalone developer HTML file.
- Select the actual instrument/Hi-Z input of the audio interface for a channel. ASIO is the singer/guitar monitor output. Start with headphones or monitors low, clean input trim and no feedback path. Do not plug guitar directly into mic-level phantom-power input.
- Establish the existing native WIRE to MAIN/output explicitly (or test existing approved startup routing). `SOURCE ONLY` should still meter at the channel but cannot reach MAIN.

## Acceptance sequence (capture yes/no + audio interface/driver settings)
1. Insert **FX Rack → Guitar FX Rig** in the intended channel; the existing native editor window should open, with working original PNG amp, gate/drive pedal and cabinet art. Verify FX slot identity stays stable through editor close/reopen.
2. Play single notes and chords, choose **Clean Studio → Warm Blues → Vintage Crunch → Modern Lead** while output is ON. Expect an audible change of guitar tone and the UI to transition from pending to DSP accepted. Compare wet vs rack bypass at low level. These names are *algorithmic* starting profiles, not emulations of a commercial amplifier.
3. Edit AMP Gain/Tone/Master and Drive, and toggle Gate/Drive. Each live tweak must reach the **active** callback; historically pending edits were drained only when the entire FX was bypassed. Do not accept a change that only occurs after turning rack OFF.
4. Observe IN/OUT meters with real guitar and then remove/disconnect the input; the editor must not display a fabricated live meter while there is no processing. Verify mute/solo/fader/PFL and WIRE behavior remain owned by the standard mixer.
5. Close editor, reopen, ensure the accepted tone matches actual DSP and reflects the last applied tweak. Save `.smix`, restart the program, Open `.smix`, confirm the exact channel FX identity and parameter values. Repeat with SOURCE ONLY then restore WIRE to MAIN.
6. Quickly change presets/parameters at 48 kHz / 128-sample buffer, listening for pops, missed ACK, dropouts and unintended sudden gain changes. Repeat at 44.1 kHz / 256 frames and, if supported by the interface, 96 kHz / 128 frames.
7. Change/reconnect the selected audio interface while the mixer remains open. Ensure a stale prepared 48kHz program cannot be installed at 44.1kHz. Accepted settings must survive without additional Monitor/ARM wizard clicks.
8. For ASIO performance, record exact interface/driver, buffer size, sample rate, actual observed round-trip latency (using loopback measurement), number of active guitar channels, xrun/dropout count, and subjective tone quality in headphones.

## Evidence and release gate
- Workflows: asset/JS contract, native registry PCM fixtures, one exact-SHA Windows build/CTest and verified ZIP. A Windows job alone **does not** prove live Hi-Z/ASIO routing or speaker reproduction.
- Provide one field test note: exact SHA/build, interface, input/output selection, `.smix` path, tested sample rates and buffers, audible clean/crunch transition, CAB on/off comparison, UI truthfulness, copy of any error.
- **BLOCK M1 release** if a normal active FX insert fails to apply live edits; if saved state does not match actual audio; if a device restart crashes, loses settings or requires hidden monitoring steps; if physical output does not route through the single native PCM graph; or if the verified Windows ZIP is unavailable.
- Deferred: editable NAM capture/IR browser, tuner, fully saved A/B, modulation/delay/reverb, manufacturer-level audio profiling. Do not show fake controls for those features in the M1 editor.
