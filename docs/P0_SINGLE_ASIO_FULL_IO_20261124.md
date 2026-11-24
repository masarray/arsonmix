# P0 Single ASIO full I/O inventory — Maono PS22 acceptance
Owner priority 2026-11-24. Independent of #825/#830; no secondary capture, new driver instance, ASRC or extra render graph.

## Evidence and boundary
- Maono official DAW setup uses one Maono ASIO driver: https://faq.maono.com/docs/DAW-Quick-Setup-Guide
- Maono software has MIC, VC and Loopback routing, but Windows endpoints do not establish the ASIO channel map: https://www.maono.com/pages/maono-software-download-center
- DAW practice distinguishes reported channels and enabled mono/stereo sources: https://www.ableton.com/en/manual/routing-and-i-o/
- JUCE exposes driver names independently from active masks: https://docs.juce.com/master/classjuce_1_1AudioIODevice.html
- Existing main opens a default 2/2 and honors saved explicit masks. Native inventory already reads all names up to the 64-pin Apply boundary; SOURCE only lists verified enabled pins. Stereo labels previously hid actual driver names. Do not confuse reported 2/2 with active 2/2.

## Implementation
Visible driver-channel inventory (not hidden in Advanced), true total/active counts, explicit select-all per direction on the same opened ASIO driver, existing exact mask Apply/ACK/readback and output safety, meaningful stereo names. Full textual diagnostic reads all reported names, even beyond the 64-pin GUI contract. No artificial Maono map, no automatic patch or additional output assignment, no new realtime work. Existing saved masks preserved. If actual driver reports 2/2, this patch will not invent additional ASIO channels; compare the installed driver with REAPER at identical rate and settings.

## Physical acceptance (PENDING)
PS22 original + official dedicated driver and Routing Center: record driver version, firmware, sample rate, full input/output names and masks in ArSonMix and REAPER. If different, investigate pinned JUCE backend vs vendor API before changing framework. If both report 2/2, inspect driver control panel/mode and obtain manufacturer confirmation; no claim of native VC support until exposed. If >2/2, explicitly select reported pins, verify Apply ACK and installed snapshot; MIC mono and VC stereo simultaneously on different ChannelIds, independent meters/FX, headphone MAIN and selected VC output where available, no loopback feedback, restart and .smix preserved. Repeat 44.1/48/96 kHz as supported, sparse pins, 2/2 interface, ASIO4ALL and driver rejection. Synthetic 8/8 fixture is not PS22 hardware proof. Coordinate #861 (diagnostics), #886 (INT-1) and #837; do not merge unqualified or overwrite active source.
