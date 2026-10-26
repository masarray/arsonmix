# ArSonMix — Windows real-hardware FAT (GUI 10CH + engine P0-12)
**Status:** Experimental; not yet qualified for professional live audio. No plugins are needed for the tests below.

## Validate the archive before running
1. Extract the GitHub Actions ZIP. Check `BUILD_PROVENANCE.json` is present.
2. Verify `sourceCommit` matches the GitHub Actions run's commit; `executable` must be `ArSonMix-GUI10CH-P012-Windows-x64.exe`, `appTarget=ARMixConsole`, and `peSubsystem=WINDOWS_GUI (2)`. The `exeSha256` must match the extracted EXE.
3. Use **Windows 10/11 x64** with its audio interface driver already installed. JUCE VST3 discovery is optional. Do not infer GUI has all ten channels physically mixed yet: the current native engine implements **Channel 1** only; the remaining strips are visual design.

## Safe test sequence — use headphones at LOW volume
1. Exit other exclusive-audio apps. Connect the intended interface and note driver/interface model, ASIO/WASAPI choice, Windows version, sample rate, and driver buffer.
2. Start the app and open **AUDIO SETUP**. Explicitly select the same intended capture and output devices. Verify channel map; leave output muted until ready. Do not enable beta independent clocks or switch endpoints while audio is running.
3. Open **SIGNAL CHECK**. First choose **OUTPUT_ONLY_TONE**, no input and **zero VST3**. Listen at a conservative level for 30 seconds. If the tone crackles, click **MARK CRACKLE immediately**, then **COPY FULL REPORT**. A bad output-only tone is important evidence that capture/ASRC processing is not a prerequisite to the fault.
4. Choose **CAPTURE_ONLY** and test microphone input for 30 seconds; check capture discontinuity flags if the driver reports them. Click **MARK CRACKLE** on a detected problem, then **COPY FULL REPORT**.
5. Choose **DRY_THROUGH** with no VST3, listen for at least 30 seconds, then repeat MARK CRACKLE + COPY FULL REPORT at the first failure. Keep analog gains reasonable to rule out acoustic clipping.
6. Repeat the above at **48,000 Hz** and **44,100 Hz** when your actual driver supports both. Record the **actual negotiated** period and rate, not only the requested setting.

## How to interpret evidence
- `AUDIO_GLITCH_WINDOW` contains up to 5 seconds of pre-marker differences from the existing 1Hz status polling. It NEVER proves the exact millisecond of a glitch.
- `WASAPI_CAPTURE_DISCONTINUITY_FLAGS` can diagnose capture problems, not direct DAC render underruns; callback lateness is a **scheduling hint**, not hardware XRUN proof.
- `ASRC_INPUT_UNDERFLOW_FRAMES` is only meaningful in opt-in dual-WASAPI mode; absent counters are `N/A`, not zero.
- `RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE` means the backend has not reported trustworthy render XRUN evidence. A clean window cannot certify zero output underruns.
- A device identity/route/mute or generation change invalidates the comparison. Do not assume an incomplete or stale window is an audio success.

## Return evidence
Include the complete **COPY FULL REPORT** output plus: driver name/version, actual interface make/model, physically connected output route, sample rate/buffer, mode (tone/capture/dry), observed noise, whether Bluetooth is involved, and ideally a loopback recording. Note that a 10-channel mixer GUI does not imply 10-channel native DSP at this stage.

## Acceptance
This is P0 **hardware qualification**, not a demo. Close GitHub issue #15 ONLY after real 0-VST3 tests across agreed device/driver/rate matrix show clean output and any remaining defects have exact reproducible provenance. Do not automatically choose the smallest buffer just because CTest and virtual ASRC soak pass.
