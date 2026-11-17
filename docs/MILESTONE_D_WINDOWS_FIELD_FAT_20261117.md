# Milestone D — Windows hardware FAT candidate (2026-11-17)

**Status:** Field qualification workpack; not proof that real audio hardware
passes. This branch intentionally changes no mixer/audio code. The candidate
is built from current protected `main`, which includes merged #753 (D1 WIRE)
and merged #754 (CI-status publisher). The public alpha.26 is unchanged.

## Build provenance
Use the owner-dispatched `/arsonmix-ci fat <exact PR HEAD SHA>` lane;
preserve source SHA, compiled Windows CTest result, package SHA256,
installer/ZIP verification, and the resulting workflow artifact. A green
software CTest is not a physical listening pass.

## Windows FAT procedure (real native audio, not simulated browser)
1. **Offline / cold start**: disconnect interface. Start, add or rename CH05,
   configure FX and BUS, create SOURCE ONLY and parallel SEND, select PRE/POST,
   Save .smix, close/reopen; exact logical ACK/readback and preserved routing.
   No fake native meters and no hidden mandatory IN/ARM step.
2. **Primary ASIO**: bind an actually enumerated interface, select verified
   IN1 → CH05 → MAIN physical outputs. Record device backend, output pin IDs,
   requested and actual rate/buffer. CH01, wizard, and record arm are not
   prerequisites. Monitor levels safely before using a live microphone.
3. **SOURCE ONLY / rewiring**: disconnect CH05 primary OUT. Its input meter
   must remain active while MAIN goes silent. Reconnect MAIN via WIRE and
   verify resumed physical PCM; no stale cable or duplicate channel audio.
4. **Parallel SEND & BUS**: CH05 → MAIN plus a parallel SEND → BUS A.
   Verify post-fader then pre-fader tap in the actual callback, SEND OFF/ON
   and BUS insert effect versus bypass, while the primary MAIN remains
   independent. Do not equate a saved-document ACK with installed PCM ACK.
5. **Graph & session recovery**: change mono/stereo, retarget/delete channel,
   change buffer/rate, unplug/rebind device and reopen .smix. Confirm stable
   ChannelId/EdgeId, no feedback or unexpected automatic physical unmute.
6. **VST3 startup**: open a verified vendor FX, save, cold start twice and
   compare the chosen product class/name to actual GUI/DSP. Test a deliberately
   interrupted restore in a disposable session; next startup must preserve
   the .smix and open safe recovery rather than repeatedly crash.
   Vendor-specific WaveShell v17 requires a licensed physical host trial.
7. **Zero-VST dry route**: test wired ASIO/WASAPI first, then separate
   virtual-cable capture / wired playback and Bluetooth case if available.
   Record callback gaps, input/output device/clock identities, actual negotiated
   format and audible glitch window; never assume low CPU means no crackle.
8. **4–8 ASIO outputs / independent STREAM**: only mark passing when such
   hardware is genuinely available. No synthetic proxy or fallback pass.

## Required evidence and stop conditions
- Native app build SHA, CI run, CTest summary, installer/ZIP hashes.
- One timestamped acceptance record per routing case, physical PCM audible/
  silent observation, requested versus applied channel/graph revision and
  actual I/O identity.
- Crash case: Windows Reliability Monitor/WER exception and faulting DLL,
  selected class/UID, ASIO backend, device epoch; never publish private
  licenses, credentials, raw customer sessions or recordings without consent.
- Stop/mark **FAIL** for actual dropout, stale or mismatched PCM ACK,
  unexpected output activation, routing feedback, unrecoverable VST3 crash
  loop, or loss of saved .smix. Never auto-promote this candidate to release.

Related field owners: #412 (physical routing), #547 (WaveShell startup),
#15 (zero-FX crackle), #748 (required-status ruleset configuration).
