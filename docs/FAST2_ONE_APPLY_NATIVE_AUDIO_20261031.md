# FAST2 — one APPLY to live native audio (2026-10-31)

Operator flow: **AUDIO I/O → ASIO/Windows Audio → actual capture + playback → verified pins → APPLY**. The separate START AUDIO control is removed. APPLY is a single explicit operator consent covering the already-existing native device transaction, native MUTE (if previous MAIN was armed), new hardware generation/route epoch verification, routing ACK, and native MAIN ARM ACK. No hidden auto-start on process boot, Show recall, device loss, or driver fallback. Emergency MUTE remains immediately available in SIGNAL CHECK and in the device header when armed.

When only routing changed, APPLY performs native MUTE ACK (if needed), routing ACK, then guarded ARM ACK; never deploys a second DSP callback, WebAudio engine, or unverified physical pins. Native failures leave hardware safe-muted. New per-pin raw PCM diagnostics from merged #256 are preserved in both modular WebView and standalone.

**P0 hardware evidence, supplied by operator 23:33 WIB**: VB-Cable CABLE Output 48k input → MAONO HP Out 48k WASAPI render, MAONO shared mix format reported 44.1k. 11,744 callbacks, zero VST3, worst DSP 289us/10ms, 0 late starts/over-budget, 23 WASAPI capture discontinuity flags, 22 candidate zero-head blocks (8,448 frames). Five-second glitch windows show 2–5 capture flags. This implicates capture continuity/dual endpoint clocks for A/B testing but does NOT prove render hardware underruns or that zero-head frames were lost samples.

**FAT**: use real MAONO DIRECT ASIO vs CABLE Output → MAONO WASAPI 0FX at matched supported sample rate/buffer; test output-only tone; MARK CRACKLE and compare counters. A first-sound software test is not a clean 30-minute hardware pass. Do not auto-enable experimental #251 de-click (heuristic zero-head also occurs in legitimate silence).

Studio Pro I/O reference: https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm.
