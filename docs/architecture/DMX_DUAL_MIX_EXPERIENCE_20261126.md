# DMX — Dual Mix Experience: audit and first vertical slice (2026-11-26)

Base: eebf113fa602280283f6d691e5263361b2a9909d, canonical PR #899. Do not edit legacy AuxBusPlan to create a second graph. The accepted DynamicPreparedMixerBlock supports FX_RETURN -> MAIN primary and FX_RETURN -> MONITOR PRE parallel, plus direct source -> both. Existing terminal role protection rejects outgoing MONITOR and duplicate directed pairs. PDC aligns declared nonzero-latency FX across sinks; DMX itself adds no delay/FIFO to primary. A post-insert PRE_FADER cue edge gives independent wet gain from MAIN return fader. Never confuse MONITOR role with hardware arm or temporary PFL. Existing LiveAudioEngine publishes actual MAIN PCM once to WASAPI; current native primary requires mapped MAIN plus separate MONITOR pins when MONITOR is present. A two-output interface (ASIO singer on sole stereo pair + WASAPI audience) needs explicit future sink routing, not a silent pin alias. No auto arm.

## UX sources / decisions
- FL Studio Mixer and Fruity Send: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm ; https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Send.htm
- Studio One Console, FX channels and Cue Mix: https://s1manual.presonus.com/ (Mixing / Cue Mix).
- Allen & Heath SQ FX send/return: https://support.allen-heath.com/hc/en-gb/articles/4402943307281-SQ-Working-with-FX
- Binding existing contextual-console PRD/ARD, approved /preview/ renderer and hardware assets. Do not create another page/skin or copy proprietary assets. Source path site/preview-v2/index.html is existing canonical assembler INPUT, not a second public preview. Only existing fader-flip surface is expanded.
- SOF native role readback: INPUT/GROUP/FX_RETURN source, GROUP/FX_RETURN/MONITOR destination; MAIN/MONITOR terminal sources excluded. Exact existing parallel EdgeId only, no auto patch, no fader write on missing edge, no new native transport. Preserve ACK/readback and reference-only demo behavior. Ordinary return fader remains MAIN; choose MONITOR in SEND MIX to edit PRE return independently. Select PRE explicitly, not POST (post would follow MAIN return level).

## Gates and remaining work
DMX1 audit complete for accepted graph and GUI SOF selector; physical endpoint mapping and session schema audit still needed.
DMX2 existing graph contract exercised with actual accepted PCM and a deterministic wet-only stand-in, one FX call/block, first-sample exact signal, independent gain/slew, stable EdgeId and terminal rejection. NOT a native plugin wet-only proof.
DMX3 explicit wet-only DSP adapter required per native spatial FX. No output-minus-dry hack, no rack bypass leaking input into an FX return, no conversion of third-party FX unless supported wet-only and safe tail policy. Coordinate with #933 Vocal Delay and Reverb-Sis; default existing inserts unchanged.
DMX4 first native role-aware SOF slice only, no wholesale redesign; real Chromium screenshot/gesture proof required on exact head.
DMX5 explicit add FX Return / create parallel WIRE to MONITOR / PRE; reuse native graph transactions. Future one-click move must transfer state, plugin ownership and validated source/return topology atomically while disarmed, with rollback. No destructive implicit conversion.
DMX6 independent physical ASIO MONITOR + WASAPI MAIN needs exact hardware FAT; sole stereo physical pair case requires explicit bus-to-sink patch and readback; no duplicate render. Keep secondary best-effort, identity, stale FIFO and mute fences.
DMX7 exact Windows build, .smix old/new roundtrip, safe restore, hardware listening and rollback still open. No installer/release claim from unit tests.

Test plan: existing logical mixer C++20, Node native contracts, actual compiled native /preview/ Chromium (new return-to-monitor scenario), exact Windows qualification and physical FAT. No new heavy workflow per edit. Prior #933 not merged here, no shared plugin changes.
