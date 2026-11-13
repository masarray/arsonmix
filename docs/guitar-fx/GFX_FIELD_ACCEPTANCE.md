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
5. Store two different **A/B** tones: adjust A, switch B, select an alternate factory tone, then return A. Verify there is no unexpected bypass, silence or rack instance replacement. Close and reopen the editor and confirm both banks remain independent. Save `.smix`, restart, Open the session, and verify exact active bank plus both tones. Prior one-tone **GRX1 v1** sessions must migrate with the original tone in both banks; malformed bank state must reject without corrupting the accepted graph. Repeat with SOURCE ONLY then restore WIRE to MAIN.
6. Quickly change presets/parameters at 48 kHz / 128-sample buffer, listening for pops, missed ACK, dropouts and unintended sudden gain changes. Repeat at 44.1 kHz / 256 frames and, if supported by the interface, 96 kHz / 128 frames.
7. Change/reconnect the selected audio interface while the mixer remains open. Ensure a stale prepared 48kHz program cannot be installed at 44.1kHz. Accepted settings must survive without additional Monitor/ARM wizard clicks.
8. For ASIO performance, record exact interface/driver, buffer size, sample rate, actual observed round-trip latency (using loopback measurement), number of active guitar channels, xrun/dropout count, and subjective tone quality in headphones.

## Evidence and release gate
- Workflows: asset/JS contract, native registry PCM fixtures, one exact-SHA Windows build/CTest and verified ZIP. A Windows job alone **does not** prove live Hi-Z/ASIO routing or speaker reproduction.
- Provide one field test note: exact SHA/build, interface, input/output selection, `.smix` path, tested sample rates and buffers, audible clean/crunch transition, CAB on/off comparison, UI truthfulness, copy of any error.
- **BLOCK M1 release** if a normal active FX insert fails to apply live edits; if saved state does not match actual audio; if a device restart crashes, loses settings or requires hidden monitoring steps; if physical output does not route through the single native PCM graph; or if the verified Windows ZIP is unavailable.
- Deferred: editable NAM capture/IR browser, tuner, fully saved A/B, modulation/delay/reverb, manufacturer-level audio profiling. Do not show fake controls for those features in the M1 editor.

## New engine performance verification
- Synthetic cabinet FIR is 65-tap symmetric, reduced to 33 multiply pairs per sample per channel. This is a code-level operation count, **not** measured wall-clock performance. Benchmark one, four and eight guitar inserts before claiming CPU savings on ASIO hardware.
- Verify spectral character and transient level by ear across 44.1/48/96 kHz. The intentional 2× antialias path does change preamp harmonics; avoid claims of bit-identical reproduction of pre-M2 audio. Measure exactly **48 samples total fixed GFX PDC** (16 preamp + 32 CAB), with the same timing for Drive/Amp/CAB bypass. Compare 64/128/256 frames in the real interface.
- Model loading / NAM captures and commercial cabinet IRs are explicitly not supported in M1; see [pro engineering decisions](GFX_PRO_WORKFLOW_AND_DSP_ARCHITECTURE.md).

## GFX Gate engineering qualification
- Feed a real guitar input with a quiet stereo echo/ambience return at one side. With Gate ON, adjust threshold close to idle-noise floor. The stereo image must remain stable because L/R attenuation is linked; no unintended dual mono gating.
- Pluck a note, vary HOLD (0–120 ms) and RELEASE (12–240 ms). Short hold/release should damp background noise faster; longer values should preserve decaying guitar notes. Compare against gate OFF using level-matched reference; no invented VU movement.
- Sweep Threshold −80…−30 dB; fast attacks must not audibly suppress the first pluck. Report pops/flutter, and device/buffer/rate changes at 44.1/48/96kHz.
- The Drive/Amp are now **2× oversampled** using fixed FIR, not native-rate. Portable high-gain 9kHz→21kHz alias improved from −16.6682 to −57.1979 dBc at 48 kHz with cabinet bypassed. Test real guitar buzz/brightness and level-match before/after switching models or bypass; no claim of NAM capture fidelity or alias-free output across the full spectrum.

## M2 2× antialiasing / ASIO performance acceptance

- Observe the exact installed build SHA after 2× antialias was merged; the earlier M1.6 FAT archive does **not** contain this processing.
- In a live Hi-Z guitar channel test clean/pedal drive/lead with CAB ON and OFF. Compare headroom, harshness/alias fizz and output loudness; the 2× algorithm must be subjectively acceptable rather than only passing a single spectral fixture.
- Repeat with 1, 4 and 8 simultaneous Guitar FX inserts at 44.1, 48, and 96 kHz, 64/128/256 frames if supported by the interface. Record ASIO interface driver, xruns, actual loopback latency and CPU; the GitHub 0.75/2.93/5.77% figures are not a guaranteed customer PC benchmark.
- Verify 48-sample DSP PDC remains matched during CAB/Drive/Amp ON→OFF, rack bypass and dynamic WIRE reconnection. Fast toggle must not output stale samples or unaligned echoes; the matched dry preamp delay remains warm even when distortion is enabled.
