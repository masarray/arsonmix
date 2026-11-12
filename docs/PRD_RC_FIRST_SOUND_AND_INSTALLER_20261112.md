# PRD — ArSonMix RC: Zero-Friction First Sound, Built-in FX and Windows Setup

**Revision:** 2026-11-12 · **Status:** approved delivery scope; implementation IN PROGRESS, NOT RC-certified  
**Integration owner:** [PR #579](https://github.com/masarray/arsonmix/pull/579) on latest `main`.  
**Architecture:** [companion ARD](ARD_RC_FIRST_SOUND_AND_INSTALLER_20261112.md), [DEVICE ARD](ARD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md), [DEVICE PRD](PRD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md), [Smart Audio blueprint](SMART_AUDIO_ENGINE_IMPLEMENTATION_BLUEPRINT.md).  
**Original screenshot defects:** Vocal Throw wheel flicker / white-on-white option list; Compressor FX chrome inconsistent; Matrix/Picker X buttons; Add FX picker footer; first-run sound absent and Save As dependent on Audio Setup.

## Outcome and non-goals

The first-run novice workflow is: install ArSonMix -> app discovers safely usable endpoints -> verified graph and speaker destination -> hear actual routed audio from installed VB-CABLE, a real hardware input, or Player without requiring a hidden Monitor/ARM wizard. The application must **never invent input samples** or play a loud probe, pick a virtual speaker as if physical, silently rewrite OS-wide defaults, bypass the PCM output fence, replace explicitly saved ASIO device selection, or claim audio sound when the input has no signal. If auto-binding is not proven, show one meaningful action rather than misleading green `OUTPUT ENABLED`.

Professional ASIO settings, explicit routing and channel FX, .smix persistence, MAIN/MONITOR role, multiple physical outputs and a real native callback remain first-class. This is a user-first policy atop the existing engine, not a second engine or a realtime auto-detection worker.

## Product journeys and screen behavior

| ID | Situation / action | Expected result |
| --- | --- | --- |
| UX01 | Launch and no audio hardware setup; save built-in FX preset | Save As / list / rename / delete work on a logical FX workspace; disk ACK; no forced setup or virtual PCM |
| UX02 | Pick any FX in rack | Picker dismisses after accepted insert and exact ChannelId/slot/instance readback, then FX editor opens; failed insert shows actionable feedback |
| UX03 | Hover wheel over rotary/slider | Value changes in bounded small steps, Shift fine step, keyboard still works, precise native ACK; no whole-window rendering per wheel pulse |
| UX04 | Scroll on Vocal Throw | THROW pad no flicker/no accidental cue; native dropdown options always legible |
| UX05 | Open Plugin Manager, Player, Routes, Matrix, Wire Setup | Center within actual app viewport on first open, move by header, keep within window, outside mixer click dismisses only transient utilities, never editors |
| UX06 | Open/close any floating utility | Same compact accessible close button token, keyboard focus, touch/pointer hit target; no duplicated JS dragging authority |
| AUD01 | Clean Windows, standard VB-CABLE installed, speakers present, no previous explicit configuration | Detect actual CABLE Output **capture**, choose uniquely verified physical render device, establish graph, restore or create explicit MAIN route, produce audio only if source has signal |
| AUD02 | VB-CABLE not installed or no capture endpoint | Do not block Player / hardware input use; show install-driver / select-input recovery, never invent capture |
| AUD03 | User previously chose ASIO or explicit I/O | Restore exact verified device/binding; auto discovery must not replace ASIO with WASAPI or change saved pin masks |
| AUD04 | Default Windows playback is CABLE Input or another virtual endpoint | Never use it as speaker render; prefer remembered verified physical endpoint, otherwise require clear user selection |
| AUD05 | Duplicate friendly names, device unplug/replug, failed opening | Require unambiguous endpoint IDs and post-open revalidation, rollback and actionable feedback; no silent output reroute |
| AUD06 | Source is NONE / SOURCE ONLY / disconnected MAIN wire | Channel meter may run while MAIN stays silent; no automatic patch to MAIN contrary to user's explicit SOURCE ONLY |
| AUD07 | Input is VB-CABLE Output and output is CABLE Input of same cable | Reject feedback loop, even when names/roles change; no monitoring override |
| INS01 | Fresh install without driver | Inform user of VB-Audio origin, donationware, admin elevation and likely reboot; distinct explicit consent; ArSonMix still installs if user declines |
| INS02 | Verified existing driver present | Do not reinstall driver or change Windows default endpoint |
| INS03 | Driver installer fails/cancelled/reboot pending | Preserve application, prior sound configuration and user session; resume/repair only with verified state, never infinite loop or unverified success |
| INS04 | Repair, upgrade, uninstall | Hash-verified release and app state; no driver removal on uninstall (may be used elsewhere); session/private files preserved |
| RC01 | RC candidate source | One reconciled exact SHA, full MSVC, CTest, Chromium UI, installer/upgrade fixtures, provenance manifest and downloads on landing page. Mark real ASIO hardware qualification separately |

## Default first-run intent ranking

1. **Explicit saved session and hardware settings** take priority. Recover only exact validated endpoint identity and pinned ASIO; never opportunistically replace.
2. **Untouched first-run Windows**: one verified standard VB-CABLE CABLE Output capture + one actual physical speakers/headphones render; prefer an *unambiguous* physical endpoint that was the Windows render default **before driver installation**, else current verified physical default; use a stable native endpoint ID and same-clock/accepted Windows Audio semantics.
3. No VB-CABLE: choose safe physical output for Player and show microphone/hardware source choices only if truly reported; never make it a mandatory step to open FX/presets.
4. Ambiguous render, ASIO exception, multiple clocks, absent hardware, unreadable OS identity: remain safe (audio idle or source-only), present one repair action; preserve any existing valid session.

No silent OS default-device reassignment. `CABLE Input` is the **Windows playback** side receiving other apps; `CABLE Output` is the **recording** side captured by ArSonMix. Other apps must be routed to CABLE Input to provide content. Auto-selection cannot manufacture content.

## Installer and licence

The **standard** VB-CABLE may be redistributed AS IS with origin attribution (`https://vb-cable.com`) and conspicuous donationware notice; **A+B and C+D packages may NOT be bundled**. Professional usage may require licences. Confirm the applicable [VB-Audio distribution terms](https://vb-audio.com/Services/licensing.htm) at packaging time; a redistributable package is NOT a proprietary dependency whose content may be modified. Never ship without the official package's exact version, hash, origin, notice and installation contract. Install driver with informed consent and administrator privilege in a separate elevation phase; expect a reboot. App's per-user installer must not silently become global admin. Reference [VB-Audio manual](https://vb-audio.com/Cable/VBCABLE_ReferenceManual.pdf) and [official download](https://vb-audio.com/Cable/index.htm).

## Release checklist / software-vs-hardware distinction

- [ ] PR #579 GUI/FX audited, quick + 2 viewport Chromium smoke PASS after final changes
- [ ] Offline preset save/read/delete works before selecting a device; exact disk readback
- [ ] Picker accepted insert -> editor opens; rejection -> no ghost editor
- [ ] All FX wheel handlers stable and bounded; Vocal Throw wheel does not trigger THROW
- [ ] Actual Windows first-run candidate policy implemented and native endpoint-ID verification, ASIO preservation and loop rejection tested
- [ ] Native graph/pin output authorization works without hidden setup when all proof is satisfied
- [ ] Driver package legally sourced, independently hash verified, consent/elevation/reboot/repair flows tested on Windows
- [ ] Main reconciled; exact SHA Windows MSVC/CTest, installer upgrade/rollback and zip provenance PASS
- [ ] Landing page points to verified RC installer ZIP/setup and portable ZIP, no GitHub navigation required
- [ ] Physical ASIO/real-user listening unverified evidence clearly separate from software RC acceptance

**Hard gate:** Never label a pending workflow, newly inserted driver archive or green quick CI as a qualified RC. Code changes and test evidence are durable, but a physical FAT is not inferred.
