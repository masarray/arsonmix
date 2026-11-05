# M3-E — First Real Sound & Exact-SHA FAT Evidence

Authority: AGENTS.md, issue #397; ONLY approved /preview/ GUI. Hardware crackle #15 is STOP-SHIP. ENGINE #266 solely owns callback PCM; INTEGRATOR #402 owns native ACK/readback; FAT only owns evidence and packaging.

## Implemented FAT intake

- Independent native Windows release packaging copies collect_hardware_fat_windows.ps1 (selectively extracted ONLY from parked #270) and m3e_fat_evidence_gate.mjs beside the EXE. No old callback/GUI is merged.
- BUILD_PROVENANCE.json records exact sourceCommit, EXE SHA256, approved /preview/ GUI asset hashes, both FAT tool hashes, and EXPLICIT UNQUALIFIED status.
- Windows verify_native_windows_artifact.ps1 compares tool bytes from the checked-out source, distributed directory and compressed ZIP entries; it verifies actual AMD64 PE32+ GUI application, not a console-test executable.
- The offline field recorder never starts audio, changes device, arms output, opens VST3 or uploads logs. It only records operator-entered evidence and cryptographic copies of full reports.
- The Node assessment validates **both 44,100 and 48,000 Hz** using OUTPUT_ONLY_TONE, CAPTURE_ONLY, DRY_THROUGH_0FX, LIVE_THROUGH_1FX; requires 30-minute wired 0FX and matched 1FX, matching driver/buffer/ports, full report hashes, human witness and honest CLEAR observations.

## Windows field procedure

1. Extract the complete exact Actions ZIP, then independently compare its sourceCommit to the GitHub run. Use a wired headset and low output level. Do not use Bluetooth as the reference output.
2. From the extracted folder, run PowerShell: ./collect_hardware_fat_windows.ps1 -Action Verify. The script fails when the EXE or FAT collector differs from the manifest.
3. In the running native approved /preview/ UI, apply an ACTUAL supported driver, physical I/O pins, clock and buffer, and explicitly ARM only through the app's verified safe workflow.
4. For each supported sample rate (44100 and 48000), record the 4 required modes with -Action Start and -Action Stop. The 1FX run requires a prior complete matching 0FX baseline. See the existing HARDWARE_FAT_GUIDE.md for mode setup; the app's COPY FULL REPORT and manual witness are required.
5. Run: ./collect_hardware_fat_windows.ps1 -Action Summary; then, when Node 20+ is available, run: node ./m3e_fat_evidence_gate.mjs .
6. Review FAT_RUNS.json and raw reports for device identifiers before sharing. Neither script transmits files. If the actual interface does not support a requested rate, keep the full two-rate evidence gate PENDING, rather than fabricating a result.

Example: ./collect_hardware_fat_windows.ps1 -Action Start -Mode DRY_THROUGH_0FX -Backend 'ASIO' -SampleRate 48000 -BufferFrames 256 -InputPins '0' -OutputPins '0,1' -CaptureEndpoint 'actual-pin-identity' -RenderEndpoint 'actual-wired-output' -Wired
After at least 30 minutes with the actual native route playing: ./collect_hardware_fat_windows.ps1 -Action Stop -Result CLEAR -Witness 'qualified-observer' -ReportPath '.\COPY_FULL_REPORT.txt'

## Three independent verdicts (non-interchangeable)

| Gate | Exact meaning | Current progression |
| --- | --- | --- |
| BUILD | Same-SHA GUI EXE + approved /preview/ byte hashes + FAT tools + ZIP. Local hashes alone do not prove external CI status. | Verifiable after the exact-head Windows package job. |
| NATIVE PCM | Real installed JUCE callback, CH1–10 + dynamic ChannelId, real fader/PAN/MUTE/SOLO, GROUP/MAIN, actual VST3 and ACK plus readback. | #402 readback projection MERGED; real installed ENGINE #266 producer + M3F accepted mutating ACK/snapshot remain PENDING. |
| PHYSICAL FAT | Recorded genuine 44.1/48-kHz wired zero-FX and FX sessions plus human independent acoustic review, including hotplug/reconnect and crackling #15. | PENDING real hardware. |

Both the collector and gate deliberately output EVIDENCE_READY_FOR_HUMAN_REVIEW at most, never hardware PASS or product release-ready. The package is experimental. A GUI screenshot, mock PCM fixture, green CTest, assumed callback, or user-entered VST3 name is NOT real acoustic evidence.

## Reliability / crackle provenance

Capture discontinuity flags, callback gap, DSP deadline and render-driver XRUN are **different** signals. In JUCE WASAPI getXRunCount can count capture discontinuities rather than render hardware underrun; do not infer clean output from zero capture flags. Unknown counters remain UNAVAILABLE, not zero. Segment reports by device epoch, driver, sample rate, buffer and actual endpoint. Operator mark and full COPY FULL REPORT are the audit source.

## Mandatory owner handoffs

ENGINE #266: one signed source-SHA+epoch numerical callback PCM capture for all original ten input strips + dynamic CH11, fader/PAN/MUTE/SOLO, wet-only FX return and real VST3 plugin lease. No second audio callback. INTEGRATOR M3F (continuing merged #402): one accepted native publisher/epoch with GUI intent, matching ACK, and exact later accepted snapshot; do not enable unsupported operations. FAT consumes their evidence only on a joined exact Windows EXE. Keep #15 open until genuinely qualified.

## Windows PowerShell 5.1 JSON interoperability

Windows PowerShell 5.1 writes UTF-8 BOM with `Set-Content -Encoding UTF8`. The independent M3-E Node checker accepts an optional BOM at the beginning of `BUILD_PROVENANCE.json` and `FAT_RUNS.json` without weakening EXE/collector/report SHA256, source-commit matching, schema validation, or `hardwareCertified=false`. Regression fixtures prove genuine BOM-encoded journals remain reviewable while malformed or cross-build records fail closed.
