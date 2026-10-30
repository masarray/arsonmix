# ArSonMiX — Hardware Audio I/O Setup (Studio-style) PRD
**Date:** 2026-10-30 | **Scope:** real native Windows ASIO/WASAPI logical-to-physical patch workflow | **Product name:** ArSonMiX

## Outcome
An operator can select their own Windows audio interface, inspect **actual enabled input/output ports** and map logical mixer sources/MAIN output using a compact, readable **M / L / R crosspoint matrix** similar in workflow to Studio One. Changes are not applied to sound until explicit **OUTPUT MUTED → APPLY → native ACK**. There are no phantom inputs, fake outputs, auto-assigned second stereo pin or preview-only LIVE indicators.

## Interaction
1. Click AUDIO I/O (also existing AUDIO SETUP action). Device identity and driver/rate/buffer come from the current native status, not a fake demo model. DRIVER SETTINGS opens the existing JUCE selection window with the Windows-available **ASIO or WASAPI** device drivers and negotiated device properties. On a failed/different device, user reviews routing again.
2. INPUTS displays **ten currently live-engine-supported logical channel rows** and the verified active physical input columns (max **eight columns per bank**, scroll/page across more), with driver port labels shown. Choose NONE/MONO/STEREO for the source independently of the channel-processing M/LR width. Clicking an unpatched cell creates a MONO draft; for stereo, choose L and R explicitly from *two different* available pins. No implicit neighbor-pin inference, including sparse/nonconsecutive ports.
3. OUTPUTS displays only **native output-capable physical pins**. MAIN LEFT and MAIN RIGHT use separate crosspoint rows, cannot use the same port. Other physical output buses must be backed by a truly compiled native render graph, not decorative matrix rows. The four FX AUX returns are **internal DSP buses**, not four extra output jacks.
4. DEVICE lists native-enumerated types and endpoints; DRIVER SETTINGS is the authoritative rate/buffer/endpoint selector. Current GUI quick-select dropdowns remain inside an accessible disclosure for users who prefer them.
5. REVERT DRAFT restores the accepted route via the existing native controller. Incomplete stereo selection blocks the modal APPLY, and closing the dialog discards its temporary incomplete choice. The valid existing live route is never touched merely by clicking the matrix. A device generation change clears the old incomplete choice.
6. Visual: restrained grey/charcoal matrix, blue DRAFT and green ACK/LIVE assignments, small font, compact rows, no blur, heavy hover shader, continuous animation or per-pointer render; generic original rack illustration is **not** passed off as the actual connected device photo.

## Boundaries and acceptance
- Present main render graph has **10 physical-input channel lanes + 4 internal stereo FX AUX**. DYN0/DYN1A are preparatory data/offline PCM work; they do NOT establish native live `+CHANNEL` or unlimited channels. Follow DYN1B/DYN2 separately.
- Supports arbitrary driver-reported **enabled** pin numbers, not made-up 2/8/24-port templates; same pin may feed multiple independent mono channels where engine permits.
- Two-output devices reject duplicate MAIN assignment. Eight-output devices page correctly, and sparse ports remain sparse.
- Safe against output armed, pending native APPLY, invalid device epoch, missing/duplicate stereo pins. Native rejection leaves accepted graph, physical mute and plugin states unchanged.
- At 1366×768/1440×900/1680×900, modal fits with scrolling, faders and FX rack remain mounted; no per-meter matrix refresh.
- Automated Node contract + Chromium Playwright smoke + current Windows CTest/GUI packaging gate before merge. **Physical ASIO/WASAPI end-to-end and zero-FX crackle FAT (#15) remain open until tested.** Never claim Studio One audio performance parity or native bus support from a screenshot.
