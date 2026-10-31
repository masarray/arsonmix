# M3E — actual live independent Group in ONE JUCE hardware callback

2026-11-01 · ArSonMix Studio Console · native pilot for physical 0FX testing

## This is a real vertical slice, not another offline-only test
New `src/LiveDynamicBusSession.h` prepares **11 existing ChannelIds (CH01–10 + MAIN) plus an independent Group**. Native `DynamicChannelRoster::createBusForSelected` rewrites the selected channels' **exclusive primary OUT** to the Group, and Group OUT→MAIN. The Group is **ChannelId #12**, not a spare CH10 channel; the input-bank size stays 10. Prepared PCM is compiled and atomically published under actual device epoch, verified input pin callback ordinals, current physical MAIN output pair, and all-outputs-disarmed gate. A failure changes nothing audible/accepted.

`LiveAudioEngine::audioDeviceIOCallbackWithContext` is still the **single registered JUCE callback**. For one accepted native Group it uses the dynamic published PCM **INSTEAD of** the existing `LiveDryRoutingLab::process` (never both); MAIN renders on actually ACKed physical L/R. The same output fence, MUTE, secondary-output guard, input PCM diagnostics, loudness/meter path and callback timing remain. Stale device epochs or invalid ports fail silent. A bounded 256-sample first-unmute Group fade protects the physical output on a structural handoff. UI Stage/ACK never performs audio processing.

The native Group has a **real fader**: browser -60..+10 dB → guarded native `ChannelId` atomic parameter → 64-sample DSP ramp. The BUS source count and named fan-in come from the **same prepared PCM revision** returned by `DynamicConsoleRoutingView`; not a fake browser assignment. Ctrl/Shift-click on existing input names selects multiple physical inputs. A compact GROUP rail shows live group ID, source names, and STOP GROUP. JS/HTML are mirrored for native and offline preview (preview is never described as audio live).

## Deliberate admission limits (do NOT silently lose the user's Show)
This opt-in M3E cutover is the first audible **dry Group** pilot: one Group, no existing VST3, auxiliary sends/returns, internal channel sends, independent MONITOR patch, or borrowed legacy BUS strips. The engine rejects these cases explicitly and preserves the original native 10CH graph without changing the user's accepted route. Dynamic VST3 lease transfer from M4 #261, MASTER plugins, multi-Group editing, persistent V5 Show and full native parameter parity remain separate. While the pilot owns PCM, old 10CH routing, FX bypass/edits, source switching and Show capture cannot falsely ACK changes not audible in the new graph. STOP GROUP (after MUTE+secondary DISARM) returns to the untouched original 10CH route. Device change immediately invalidates the Group and prevents stale output.

## Physical FAT protocol — Maono PS22 / normal Windows endpoint
1. Use a Windows executable built from **this exact branch HEAD**, not an earlier main artifact; otherwise this feature is absent.
2. Configure and **APPLY** verified ASIO Maono / WASAPI device and capture pins. Native status must read back actual driver + output L/R; choose and APPLY input source CH01 and CH02, verify raw input PCM with no FX.
3. **MUTE hardware MAIN and DISARM secondary stream**. Ctrl-click CH01 and CH02 names in Mixer, choose **+ BUS FOR SELECTED**. Verify positive **native ACK**, Group ID **12**, fan-in **CH1+CH2**, and Group OUT→physical MAIN.
4. ARM MAIN with the existing verified audio-safety workflow, headphone volume low. Listen to two inputs; compare direct mix before Group with Group unity: no doubled dry. Move native Group fader to -6 dB: MAIN signal should drop about 6 dB in both channels without graph restart/crackle.
5. MUTE output and **STOP GROUP**. The exact previous 10CH accepted route must return. Switch output pins or restart device; stale Group status must disappear, not route to unexpected hardware.
6. Record whether crackles occur with **0 VST**, along with driver identity, rate/buffer, capture discontinuities, raw input PCM, callback cadence and output routing. #15 is **not** closed by a successful CTest.

## Next meaningful milestone (M4→M5)
Finish #261 unified live VST3 CH1–10 leases, then move the SAME leases into the dynamic ChannelId slot adapter so a real **new Group can own compressor/EQ** and FX Return can host reverb. Convert monitor/AUX/Show snapshot by stable ID in one accepted transaction and turn off this restricted-pilot gate only when native PCM+vendor chunk+device-epoch tests pass. Then offer true standalone Add Channel/Add Bus/Add FX with keyed strips and professional comprehensive routing. Preserve one callback and no DAW timeline, recorder, OBS-only output, duplicated graph, or extra latency claims.

## Evidence gates
- New 10 input+MAIN+Group (12 IDs) native PCM regression proves exclusive primary Group routing, unity/no double dry, actual Group live -6 dB, output-armed rollback and stale-epoch fail-silent.
- Existing full Windows MSVC native CTest + Chromium/standalone JS parity must pass on exact PR head before merge. Physical Maono FAT remains not performed in CI.
