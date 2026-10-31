# FAST1 — First Sound & fast hardware routing (2026-10-31)

## User finding and regression
DEVICE4 already presents named Mono/Stereo inputs and native hardware pins, but Audio Setup's output remained MUTED and required a second hidden SIGNAL CHECK workflow. Named MIC/MIC2 rows showed identical `→ CH03` (the globally focused target) even though actual native routes were CH01/CH02. READY was also incorrectly described as audio audible.

## First Sound operator contract
- The user chooses a verified DIRECT ASIO / Windows Audio interface and clicks **START AUDIO**. This *explicit operator action* is the only authorization to arm hardware; no implicit unmute on launch, device reopen, saved Show recall, or hotplug.
- If a native-accepted physical MONO/STEREO input route already exists, skip structural apply, send native ARM with exact device epoch/revision, and wait for `arMixAudioArmAck`. If topology is new and empty, stage CH01 from the first genuinely enabled capture port and CH02 from the second if available. Perform ONE existing muted `routingDryApply`; wait for a positive native routing ACK with both requested sources and exact epoch, THEN issue ARM and verify it natively. No alternative DSP, no external audio APIs, no guessed stereo pairs, no unmute on a stale route.
- Native `liveOutputArmFault` blocks MAIN arming unless a verified open DIRECT driver, two real hardware output pins, ready/accepted physical input graph and no in-progress handoff exist. Panic **MUTE OUTPUT** remains unconditional. Isolated output tone diagnostics continue to work.
- Hardware profile labels use actual `LIVE ON CHxx` readback (or `NOT ASSIGNED`), plus explicit `ASSIGN CHxx` command. Studio I/O named sources remain per-device aliases in local storage in this milestone; **not** falsely described as native stateful Show persistence.
- Signal Check prioritizes hardware/output mute before route readiness, and the footer distinguishes `NATIVE UNPATCHED · SILENT` from native accepted route. No claim that ACTIVE driver or positive routing ACK proves acoustic sound.

## Tests and manual FAT
Node operator contract, browser test covering existing route → native ARM → native ACK → MUTE, standalone parity, Windows native MSVC + CTest. Inspect negative ACK, stale epoch, no input pin, no 2-channel output, driver hotplug, and optional output-only tone. A real host FAT with VB Cable's playback `CABLE Input` → capture `CABLE Output` and Realtek output, and ASIO direct, is mandatory. `#15` zero-FX WASAPI crackle remains OPEN until hardware captures/clock diagnostics qualify it.

## Why not auto-unmute at launch?
A saved endpoint may disappear or become a different speaker/headphone system. A live mixer should never unexpectedly energize amplifiers. FAST1 eliminates the multistep unmute journey by making it one intentionally requested, verifiable START action while maintaining predictable emergency MUTE.

## Studio One / Fender Studio Pro reference
https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm — named software I/O rows map hardware columns, default I/O profile; https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Set_Up_Your_Audio_Device.htm — sample rate, clock/device and buffer selection. FAST1 makes First Sound straightforward; durable native per-device I/O session profile is a separate #205 milestone.
