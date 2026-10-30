# ArSonMix — MS3A + G5E Integrated Windows Operator FAT
**Milestone:** physically routed MAIN + native all-routed-channel VST3 Show Save/Restore + output-correct de-click/loudness
**Source:** `main@20cd7878614e90135efeb23d45784c7fb832b1aa` (2026-10-30), [MS3A #198](https://github.com/masarray/arsonmix/pull/198), [DYN1A #199](https://github.com/masarray/arsonmix/pull/199), [G5E #197](https://github.com/masarray/arsonmix/pull/197), [MS3A2 #201](https://github.com/masarray/arsonmix/pull/201).
**Status:** production-feature integration candidate, *not yet qualified for live sound*. Critical device 0FX crackle [#15](https://github.com/masarray/arsonmix/issues/15) remains OPEN. Neither success in GitHub Actions nor a software meter can certify clean ASIO/WASAPI audio. The `DYN1A` prepared PCM kernel is offline; the audible application still owns 10 input strips plus 4 internal FX AUX returns, not a live dynamic +CHANNEL GUI.

## 0. Get the correct Windows native build — no need to clone/compile

1. Open [canonical Windows Actions artifacts](https://github.com/masarray/arsonmix/actions/workflows/p1-windows.yml) and select a **completed SUCCESS Windows Native VST3 Host** run on `main` at SHA `20cd7878614e90135efeb23d45784c7fb832b1aa` or a newer successfully combined `main` build. If a newer main run is PENDING, do not treat the earlier PR-only binary as a certified combined build.
2. Under Artifacts, download `ArSonMix-GUI10CH-P012-Verified-Hardware-FAT-Windows-x64`, extract the **whole archive to a dedicated folder**, and open `BUILD_PROVENANCE.json`. Check `sourceCommit` is *exactly* the run SHA and the executable is `ArSonMix-GUI10CH-P012-Windows-x64.exe` with `appTarget=ARMixConsole`, `peSubsystem=WINDOWS_GUI (2)`. The name “Verified Hardware FAT” means **artifact packaging verified**, *not sound tested*.
3. Optional integrity check in PowerShell from that folder:
```powershell
$expected = (Get-Content .\BUILD_PROVENANCE.json -Raw | ConvertFrom-Json).exeSha256
$actual = (Get-FileHash .\ArSonMix-GUI10CH-P012-Windows-x64.exe -Algorithm SHA256).Hash
if ($actual -ne $expected) { throw 'Wrong or altered EXE — do not run' }
```
4. Windows 10/11 x64 and the **Microsoft Edge WebView2 Runtime** are required. Open the actual EXE, not `preview/mixer.html` or a web demo. A web preview never proves hardware output or native VST3. The archive is portable: `portable_mode.txt` means the user-owned `Studio` directory is created beside it; ZIP does not bundle commercial plugins or the owner's presets.
5. **Safety:** use headphones at low analog volume, disable loudspeakers/high-power amplifiers, keep physical OUTPUT **MUTED** for every structural APPLY/SAVE/RECALL, and explicitly review the chosen physical pins before any manual unmute. Never auto-unmute after a device restart.

## A — Physical 0FX baseline (the hard release gate)

Use **AUDIO SETUP** to select a verified DIRECT ASIO or WASAPI full-duplex interface, real enabled capture/output pins, and negotiated 48 kHz / 256 samples if supported (record actual values). Start **SIGNAL CHECK**; at a low level test **OUTPUT ONLY TONE → CAPTURE ONLY → DRY THROUGH**, **30 seconds each with zero VST3**. Repeat at 44.1 kHz and/or alternative buffer if supported. Capture user-perceived crackle, pop, mouse/UI sluggishness, physical clipping, channel/mute behavior; select **MARK CRACKLE → COPY FULL REPORT** immediately on a glitch. The backend may be unable to report real render XRUN: `RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE` is **not** a zero error count. A clean software trace is not a hardware proof.

**PASS:** user hears no output discontinuity, silence while muted, no stale patch restoration after disconnect; evidence includes interface/driver, Windows version, sample rate, buffer, physical ports and report. **FAIL:** any audible 0FX crackle; keep [#15](https://github.com/masarray/arsonmix/issues/15) open with evidence.

## B — MS3A/MS3A2 actual MAIN hardware output selection

A **2-output interface** has only the real output pair and cannot demonstrate reassignment to other sockets; it still validates truthful port inventory and rejection of phantom outputs. For meaningful sparse-output validation use a verified interface with active physical output IDs like `0/1/4/5` (presented as device outputs **1/2/5/6**). The driver may list different ports; do not invent disabled channels.

1. Keep **OUTPUT MUTED**. In **AUDIO I/O → OUTPUTS**, select `MAIN LEFT=HW5`, `MAIN RIGHT=HW6` if those exact physical output pins are reported. In **INPUTS**, patch CH1 to one verified mic/line capture pin. Select a channel processing format independently of the physical SOURCE format. Press **APPLY · MUTED**.
2. Confirm routing state is *DRAFT/PENDING* until **positive native ACK**. The accepted MAIN pair must show **5/6**, not the original 1/2. Verify channel fader/trim, AUX sends, master control and FX rack are still available.
3. At minimum volume, route headphones/loopback to the selected **physical** pair and manually unmute. Check the new pair carries the intended MAIN signal and the original HW1/2 pair remains silent. Ensure the master meter LUFS/dBTP reflects the **selected physical PCM after output de-click**, not the now-silent HW1/2. A short/long meter integration delay is expected; do not interpret fresh LUFS numbers on silence as active routing.
4. While **MUTED**, reverse MAIN L/R assignment and APPLY; confirm hardware sides swap only after ACK. Attempt duplicate same port L=R or disabled/nonexistent port. It must **REJECT** without changing accepted graph, output mute or physical identity. Retry a *stale device epoch* after unplug/reconnect: no old route may auto-unmute. Do not test by hot-unplugging a live power amplifier.
5. Trigger a native CH1 FX **MOVE** or change a routed FX graph only using supported safe controls. During crossfade, the signal on **HW5/6** must de-click; HW1/2 must remain silent. Report any audible pop/crackle. MS3A2 guarantees the code chooses the leased graph's output pair, not that every third-party VST3 is click-free.

**PASS:** selected pair only, correct left/right, accepted ACK, rejected invalid pair, muted on hardware changes, output meter follows audible PCM. **FAIL:** signal or meter still tied to HW1/2, pop on selected pair, duplicate port accepted, output auto-unmutes.

## C — G5E actual VST3 Show Save/Restore (not browser presets)

G5E saves binary `getStateInformation()` of **routed** native VST3 instances across the ten input slots and four AUX return lanes. The standalone **legacy borrowed CH1 plugin rack** is a **separate ownership path and currently excluded**: remove/disable that *legacy* chain for this test. Routed CH1 may still participate if a real routed native VST is available, but do not confuse preview-only plugin names with a native VST.

1. With the interface DIRECT, OUTPUT **MUTED** and native route **ACKED**, load a user-owned real VST3 into a **routed** channel (for example CH2) and another into **AUX1**, if available. APPLY with the native FX draft, verify catalog identities and native FX count. CH2 and AUX1 must use independent VST3 instances even if they select the same plugin.
2. Open each VST3 vendor editor, set a reproducible parameter A (for example mix 20%, threshold -12 dB), and **close the vendor editor**. Record exact before-values, bypass and insert order. Make a SECOND stable Show test with 2 routed inputs if your hardware has the ports.
3. With output still MUTED and no legacy CH1 FX chain, choose **ROUTING → SAVE SHOW**. Wait for **SHOW SAVED + VERIFIED**, then **BROWSE SHOWS**. This is native `Studio/Shows`, **not** UI `localStorage` or MY SOUNDS preview. The message must show a valid native Show ID.
4. Change the VST3 parameter to B (for example 80%), bypass a routed insert and change a supported route while muted, APPLY and ACK. Close editor, **RECALL SELECTED** from the saved native Show.
5. Confirm **SHOW RESTORED** + OUTPUT still **MUTED**, accepted physical input pins, MAIN output pair, PRE/POST AUX sends, plugin ordering, bypass and the vendor state parameter A again. Only then manually unmute at low volume and verify sound. Saving with a legacy CH1 rack, missing plugin/catalog identity or vendor state capture failure must **REJECT** instead of producing an optimistic Show.
6. Repeat negative gates: attempt recall while output is armed, change device epoch / remove selected VST3, corrupt a *copy* of a stored Show binary hash, or run simultaneous Show IO. Each should reject without replacing the previous audible graph or unmuting. Do not corrupt the only production Show or user's own VST3 files. Import/export portability across machines and third-party preset compatibility require separate FAT.

**PASS:** verified routed VST3 binary state A restored on real plugin, accepted hardware mapping and AUX, no auto-unmute. **FAIL:** only UI labels restored, parameter remains B, wrong plugins shared across channels, missing Show hash silently accepted, master routing auto-unmutes.

## D — Standard-computer GUI responsiveness and dynamic-channel truth

Test on the standard GPU/CPU Windows PC where pointer movement previously felt heavy, ideally 1366×768 and a normal integrated GPU. With native audio monitoring on **and then muted**, move the cursor continuously across strips, FX rack, OUTPUTS matrix and toolbar; select CH1↔CH10 repeatedly, drag faders/PAN and use BROWSE SHOWS. The console should keep fader, meter and VST rack DOM stable on *focus* changes and should not create repeated full mixer layouts during idle mouse movement. Compare with [Performance Lab](https://masarray.github.io/arsonmix/preview/perf.html) using optimized and meters-off on that **same device**; browser rAF samples are **not** a measured human input-to-paint latency guarantee.

DYN1A supports dynamic **offline PCM** compilation and stable ChannelId in native tests; the live application still has **10 input channels**. There is **no working user-facing +CHANNEL, 128CH VST3 or physical 128-input promise yet**. DYN1B must replace one live PCM graph without regressing 10CH/AUX/CH1/Show; DYN2 must then provide native-ACKed add/remove/duplicate and an efficient banked/virtualized GUI. No mock +CHANNEL should be graded as a pass.

## Return exact FAT evidence (copy this compact record)

| Field | Record |
|---|---|
| SourceCommit / EXE SHA256 | … / … |
| PC CPU / GPU / OS / driver version | … |
| Audio interface + DIRECT backend | … |
| Actual physical INPUT / OUTPUT ports | … |
| Negotiated Hz / samples / ms (if measured) | … |
| 0 FX: OUTPUT TONE / CAPTURE / DRY (PASS/FAIL) | … / … / … |
| OUTPUTS MAIN route selected, ACK, old outputs silent | … |
| CH1 vs CH2 + AUX1 PRE/POST and physical output | … |
| G5E CH2/AUX1 vendor binary A → B → A, Show ID | … |
| Device disconnect, rejected APPLY, OUTPUT remains MUTED | … |
| Cursor movement/drag on standard PC | … |
| Crackle? COPY FULL REPORT | … |

**Release meaning:** CI gates static source, C++ PCM tests, Windows GUI executable target, SHA256 embedded resources and ZIP integrity. The table above is **physical acceptance**. Do not mark #15 hardware qualified, latency certified, Studio One/LV1 feature parity, or DYN2 dynamic channel feature complete until their distinct evidence is captured.
