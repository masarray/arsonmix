# ArSonMix — Windows real-hardware FAT (GUI 10CH + engine P0-12)

> **CURRENT FIELD QUICKSTART — 31 OCT 2026 / FAST1 + P0:** This packaged file is retained under its historical filename, but **FIRST SOUND is now a one-click AUDIO I/O → START AUDIO operation** after driver/device selection and verification. The old manual INPUT PATCH workflow below is an **advanced override/negative test**, NOT required for the initial audible smoke test. Output ARM always requires a deliberate user click; emergency MUTE is immediate, and device/Show reopens never auto-unmute.

### FAST1 current first audible route (physical FAT, not a release certification)

1. Verify `BUILD_PROVENANCE.json/sourceCommit` equals the downloaded GitHub Actions run SHA and the EXE hash matches its manifest. Prefer a Windows artifact **after merged #247** to obtain capture continuity counters; old ZIPs do NOT contain those counters.
2. Start with zero VST3 and low wired headphone volume. AUDIO I/O → DEVICE/DRIVER: choose the actual installed **ASIO** or **Windows Audio** backend, correct *capture* and *playback* device, real enabled input and two output pins. Check negotiated sample rate/buffer. Do **not** assume a CABLE or Bluetooth endpoint is always required.
3. Click **START AUDIO** once. FAST1 stages CH01/CH02 from verified ports **only if the accepted graph is empty**, requires native routing ACK, and explicitly requests hardware ARM with device-epoch/revision checks. On an already accepted custom route, it preserves the route and only ARM-requests it; no duplicate patch. The status must show route accepted **and** hardware armed. If rejected, correct the exact diagnostic before retrying; never force ARM or bypass the output safety gate.
4. For VB-Cable loopback: source application Windows playback **CABLE Input** → ArSonMix capture **CABLE Output** → user-selected physical playback (e.g., MAONO HP Out). A source playing directly to MAONO is NOT captured by CABLE Output. Inspect actual raw capture PCM, routed MAIN PCM, output mute, MASTER mute/gain and real pin ACK separately; no audible sound cannot be diagnosed by a green device label alone.
5. Open **SIGNAL CHECK**. Record input/Master peak, native backend and actual device identity, `backendXrunCount` with capture-only provenance, `captureZeroHeadCandidateBlocks`, `captureZeroHeadCandidateFrames`, `captureNonFiniteSamples` (if present), driver period/deadlines, and ARM state. When you hear a crackle, press **MARK CRACKLE**, then **COPY FULL REPORT**; repeat across at least five distinct user-marked windows. Zero-head values are **candidates from exact-zero PCM**, not proven lost samples, OS xruns or real DAC render underruns.
6. Do three **0FX A/B paths** at matched hardware-supported rate/buffer: (A) same-device MAONO wired full duplex via native ASIO if installed, (B) VB-Cable → MAONO Windows Audio/WASAPI, (C) safely isolated OUTPUT_ONLY_TONE on MAONO without capture. If C crackles, prioritize render path; if only B fails while capture flags rise, investigate WASAPI capture/independent clocks. For mode/clock switches, manually **MUTE OUTPUT**, use the supported **CLEAR PATCH** procedure, confirm new endpoint epoch, then reassign/re-ARM; an active route intentionally blocks unsafe clock changes. Do not assume experimental ASRC was engaged when the UI refused it.
7. Gate: no audible clicks/crackle on the supported wired **zero-FX** route for **at least 30 minutes**, then FX/presets/device lifecycle tests, measured actual latency if available. Software CTest/Chromium PASS or 0 over-budget DSP callbacks do **not** establish hardware quality.

**Measured issue #15 baseline (21:59 WIB, 31 Oct 2026):** 0 VST3, VB-Cable capture → MAONO HP 1/2, Windows Audio 48 kHz/480 frames, 8,126 callbacks, DSP worst 375 µs, **113 WASAPI capture discontinuity flags**, real input and MAIN PCM confirmed and output ARM active. DAC render underruns were **not** measured. Avoid blaming UI/CPU/VST without evidence. [Field evidence and next A/B](https://github.com/masarray/arsonmix/issues/15).

---

> **CURRENT MS3A+G5E UPDATE (30 Oct 2026):** This is an older, preserved P0 diagnostic test plan. For the actual merged native hardware MAIN output selection, routed VST3 binary Show SAVE/RECALL and correct selected-output LUFS/de-click behavior use the **[integrated MS3A+G5E Windows operator FAT](INTEGRATED_MS3A_G5E_OPERATOR_FAT_20261030.md)**. Legacy instructions below calling MAIN output "read-only" or requiring **all** routed VST3 removed before Show Save have been superseded. Native borrowed CH1 legacy FX **must still be absent** for native routed Show capture. Neither document certifies 0FX hardware stability.


> **2026-10-30 UX/HARDWARE FAT EXTENSION:** See [new PRD](PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md) + [ARD](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md). In addition to below 0-FX and DRY Show checks, the next Windows build must be tested for MIX FX/ROUTE/SENDS toggles without lost CH1 plugins, custom hardware mono/stereo M/L/R inputs, truthful HW OUT1/2 (and no phantom extra OUT), four PRE/POST AUX sends and returns. Only native ACK may make a route LIVE; physical-output AUX/MON separately gated until actual engine. Zero-FX #15 remains unqualified.

**Status:** Experimental; not yet qualified for professional live audio. No plugins are needed for the tests below.

## Validate the archive before running
1. Extract the GitHub Actions ZIP. Check `BUILD_PROVENANCE.json` is present.
2. Verify `sourceCommit` matches the GitHub Actions run's commit; `executable` must be `ArSonMix-GUI10CH-P012-Windows-x64.exe`, `appTarget=ARMixConsole`, and `peSubsystem=WINDOWS_GUI (2)`. The `exeSha256` must match the extracted EXE.
3. Use **Windows 10/11 x64** with its audio interface driver installed. JUCE VST3 discovery is optional. **R0B native 10CH + 4 AUX starts with the output safe-muted.** Once real hardware is selected, the operator's explicit **START AUDIO** request automatically stages a first valid CH01/CH02 patch if none exists, waits for native routing ACK and then requests a guarded output ARM. For non-default/advanced routing the INPUT PATCH → APPLY controls remain available. **Never equate active driver or ACK with verified physical sound.**

## GUI failure: "Navigation to the webpage was canceled"
The offline mixer needs **Microsoft Edge WebView2 Runtime** (not Internet Explorer). JUCE can silently fall back to IE if its loader or runtime is unavailable. New builds statically link the WebView2 loader into the EXE, check browser-data-folder access, and require a real native JavaScript readiness handshake. That means no white browser error should be presented as a successful GUI load.

Extract the **whole** newest release ZIP, check `BUILD_PROVENANCE.json` says `webView2LoaderMode=JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1`, and launch the EXE. If the new error dialog reports WebView2 missing, install or repair **Microsoft Edge WebView2 Runtime** using the official page https://developer.microsoft.com/en-us/microsoft-edge/webview2/. Restart ArSonMix. Do not delete `Studio`, presets, VST3, or saved sessions to repair the browser.

The static loader is NOT the Chromium Runtime. Runtime installation and a physical Windows GUI startup test remain prerequisites to certification, even after GitHub CI passes.

## Safe test sequence — use headphones at LOW volume
1. Exit other exclusive-audio apps. Connect the intended interface and note driver/interface model, ASIO/WASAPI choice, Windows version, sample rate, and driver buffer. **Copy the entire ZIP to a new folder**; do not launch the EXE while still inside the archive.
2. Start the app and open **AUDIO SETUP**. Explicitly select the intended capture and output device. Verify actual enabled ports; leave OUTPUT MUTE engaged. Native ROUTING starts with **UNPATCHED / SILENT**, which is correct. Do not enable beta independent clocks or switch endpoints while audio is running.
3. Open **SIGNAL CHECK**. First choose **OUTPUT_ONLY_TONE**, no input and **zero VST3**. Listen at a conservative level for 30 seconds. If the tone crackles, click **MARK CRACKLE immediately**, then **COPY FULL REPORT**. A bad output-only tone is important evidence that capture/ASRC processing is not a prerequisite to the fault.
4. Choose **CAPTURE_ONLY** and test microphone input for 30 seconds; check capture discontinuity flags if the driver reports them. Click **MARK CRACKLE** on a detected problem, then **COPY FULL REPORT**.
5. Choose **DRY_THROUGH** with no VST3, listen for at least 30 seconds, then repeat MARK CRACKLE + COPY FULL REPORT at the first failure. Keep analog gains reasonable to rule out acoustic clipping.
6. Repeat the above at **48,000 Hz** and **44,100 Hz** when your actual driver supports both. Record the **actual negotiated** period and rate, not only the requested setting.

## Fast native 10CH smoke test — after isolated audio checks

1. Keep **OUTPUT MUTED**. Open **SIGNAL FLOW → INPUT PATCH**. Confirm the device identity, current epoch and actual hardware input labels. A fresh native graph has **no** active patch.
2. Select one physical microphone/line input for **CH1**, choose MONO and click **APPLY ROUTING**. The UI must change from DRAFT to LIVE **only after native ACK**. A stale/invalid port must return a rejection with existing LIVE state unchanged.
3. With headphones at minimum level, deliberately unmute OUTPUT. Move CH1 fader and verify the actual input reaches stereo MAIN L/R with no VST3. Verify CH2–10 remain silent until explicitly assigned.
4. MUTE again. Assign a **different real physical port** to CH2 when available; verify independent CH1/CH2 faders, mute, solo and pan. Do not invent extra ports for 2-input interfaces. Try CH1→AUX1 PRE and CH2→AUX1 POST in INTERNAL / SENDS; verify DRAFT vs native ACK and a real AUX return. AUX returns are **not** independent physical output sockets.
5. With output muted, load one *owned* VST3 on the CH1 rack, then exercise FX ADD, MOVE, bypass and REMOVE. Re-enable output conservatively between edits and listen for transient clicks. Report VST name/version and whether its own latency/PDC mode is active. No automatic plugin installation is required.
6. Use **CLEAR PATCH** to return to native unpatched silence. Restart or change the physical device only while muted. No previous CH1 audio may reappear automatically on a different endpoint.
7. If any burst/click occurs, use **SIGNAL CHECK → MARK CRACKLE → COPY FULL REPORT** immediately. Do not mark the app hardware-qualified just because 50/50 CTests passed.

**Rapid evidence to send:** screenshot of AUDIO SETUP showing actual device/rate/buffer; INPUT PATCH before/after LIVE ACK; short result for 0 VST3, CH1+CH2 independent route, AUX send, FX ADD/MOVE/REMOVE and CLEAR PATCH; full glitch report on failure.

## Native Show SAVE / BROWSE / DRY RECALL acceptance (P4 #163)

Perform this only after 0-VST3 output-only/capture-only/dry-through checks above.

1. Select the real DIRECT interface, leave **OUTPUT MUTE ON**, choose one CH1 physical input and, if available, a second port for CH2. APPLY INPUT PATCH and confirm LIVE only after native ACK.
2. In INTERNAL / SENDS, enable AUX1, select CH1→AUX1, create POST and optionally PRE, set level/pan, and APPLY while muted. Do not assume AUX1 is a physical output jack.
3. Remove **every VST3** from CH1 and other lanes. Click **SAVE DRY SHOW**. A real native Show must receive a successful save ACK; it is stored in portable `Studio/Shows` with its verified generation, not browser localStorage.
4. With OUTPUT still MUTED, change a strip fader or modify a routing draft and APPLY. Click **BROWSE SHOWS**, select the saved Show UUID/name, click **RECALL SELECTED DRY**. Verify the original CH input mapping, AUX PRE/POST sends and MASTER linear/MUTE/MONO settings are restored **only after positive native recall ACK**. Speakers must **remain MUTED** afterward.
5. Negative gates: selecting a Show while OUTPUT is unmuted must not send recall; while native Show I/O runs, UNMUTE must be rejected, but emergency MUTE must always work. Physically change/replug the selected interface (while muted): stale epoch/revision or missing physical ports must reject recall, never revive an old route or auto-map the next device.
6. With one vendor VST3 loaded, attempting DRY recall must fail closed; full VST-equipped Show restore is NOT implemented. After removing it, retry DRY recall. Listen at low headphones volume only after explicitly checking current physical routing and unmuting.
7. Record **PASS/FAIL** for SAVE→BROWSE→RECALL, stale-device rejection, MUTE preservation, 10CH/AUX PRE/POST preservation, and MASTER controls. Any audible pop/crackle means hardware FAT remains failed regardless of green CI.

## How to interpret evidence
- `AUDIO_GLITCH_WINDOW` contains up to 5 seconds of pre-marker differences from the existing 1Hz status polling. It NEVER proves the exact millisecond of a glitch.
- `WASAPI_CAPTURE_DISCONTINUITY_FLAGS` can diagnose capture problems, not direct DAC render underruns; callback lateness is a **scheduling hint**, not hardware XRUN proof.
- `ASRC_INPUT_UNDERFLOW_FRAMES` is only meaningful in opt-in dual-WASAPI mode; absent counters are `N/A`, not zero.
- `RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE` means the backend has not reported trustworthy render XRUN evidence. A clean window cannot certify zero output underruns.
- A device identity/route/mute or generation change invalidates the comparison. Do not assume an incomplete or stale window is an audio success.

## Return evidence
Include the complete **COPY FULL REPORT** output plus: driver name/version, actual interface make/model, physically connected output route, sample rate/buffer, mode (tone/capture/dry), observed noise, whether Bluetooth is involved, and ideally a loopback recording. State how many physical input channels were actually enabled; the native mixer supports **10 strips + 4 stereo AUX returns** but does **not** imply that every soundcard has ten capture or independently patched hardware output ports. OUT patching beyond MAIN L/R is not yet delivered.

## Acceptance
This is P0 **hardware qualification**, not a demo. Close GitHub issue #15 ONLY after real 0-VST3 tests across agreed device/driver/rate matrix show clean output and any remaining defects have exact reproducible provenance. Do not automatically choose the smallest buffer just because CTest and virtual ASRC soak pass.

## M4 — real stereo meters and multi-channel native VST3 FAT (1 Nov 2026 candidate)

**These steps apply to the experimental M4 PR build and supersede only the older CH1-only insert smoke description above.** This does not certify Studio One parity, physically clean audio, latency compensation or hot structural plugin swapping. Use the SHA in `BUILD_PROVENANCE.json` to identify the exact Windows executable being tested. Keep headphone output LOW.

1. Select one verified DIRECT ASIO interface (preferred same-clock input/output) and its *actual* input and hardware MAIN pins. Keep MAIN output MUTED during structural routing changes. Choose CH1 = MONO pin 1, CH2 = STEREO pins 1/2, CH3 = STEREO pins 1/2 if the device offers those pins. Multiple software strips may intentionally receive the same physical source; this does not invent new driver ports. APPLY the native patch and require positive epoch/revision ACK.
2. **0 FX PCM and meter:** with clean stereo L≠R source material (e.g. only left plays, then only right plays), unmute at low volume. CH2/CH3 stereo L/R meter columns must move independently. CH1 MONO may legitimately show equal peak L/R; master L/R must reflect actual independent output. Fail if both stereo bars always mirror with unequal source material. Confirm mute, solo, pan, and fader influence the real MAIN output, not merely GUI.
3. MUTE MAIN; open the CH1 FX RACK when the old legacy CH1 chain is **empty**. It must expose the native route/inserts like CH2/CH3. If an existing legacy CH1 VST3 is installed, its original state must remain available and must not be processed twice.
4. **FX isolation:** with output muted, add one zero-latency VST3 to CH2 and another different zero-latency VST3 to CH3. Stage both before APPLY. Both independent draft badges/selections must remain visible; issue **one APPLY**, verify a positive native ACK and that each channel shows its exact own VST3 ID/instance. On unmute, bypass CH2 effect: CH3 effect must remain active, and vice versa. Repeat add→reorder→remove and open each vendor editor. Do not assume an ACK proves audible sound—listen and compare dry vs wet.
5. **Negative gates:** unpatched CH4 opens verified INPUT PATCH, not a nonfunctional FX picker; when the driver changes or OUTPUT is armed, structural APPLY must be rejected without altering accepted FX state; original VST3 instances/tails on unaffected channels must not be reconstructed. On failure, revert the draft, do not repeatedly restart the app or clear saved presets.
6. Run 0FX first and only proceed to FX if physical audio is clean. Capture full SIGNAL CHECK report and MARK CRACKLE at the first click. For practical qualification repeat at 48 kHz and, when supported, 44.1 kHz, with actual buffer size shown. At least 30 minutes clean on the chosen wired configuration is required before describing that configuration as stable.

**Return evidence:** screenshot of CH1–CH3 native patch ACK, separate stereo L/R meters (left-only and right-only signal), distinct FX names in CH2/CH3, one APPLY/ACK, CH2 bypass A/B, device/rate/buffer, and full diagnostic log if audio crackles. Report whether sound demonstrably passes through each VST, not merely whether its button changes.
