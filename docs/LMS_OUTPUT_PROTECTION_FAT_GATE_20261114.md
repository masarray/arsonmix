# ArSonMix LMS — M10 speaker-protection qualification and independent hardware FAT

**Engineering state (2026-11-14): DRAFT / PHYSICAL OUTPUT DISCONNECTED.**
This decision record is for **ArSonMix** Loudspeaker Management. **Harmonice** is only the built-in vocal harmony FX, not the product name.
Track PR #635 and issue #636. No code in M2–M10 provides a physical loudspeaker LIVE/ARM capability.

## Verified professional workflow references

- [Allen & Heath AHM Channels: zone ways, individual output limiter, threshold/attack/release and gain-reduction meter](https://support.allen-heath.com/hc/en-gb/articles/40185199750161-Channels)
- [Allen & Heath AHM Configure: output patching after selecting crossover ways and structural interruption warning](https://support.allen-heath.com/hc/en-gb/articles/40185032073873-Configure)
- [Q-SYS Loudspeaker block: request manufacturer RMS/peak voltage and detector timings](https://help.qsys.com/Content/Schematic_Library/lua_speaker.htm)
- [Q-SYS Gain Structure: measured amplifier calibration, limiter downstream of processing](https://support.qsys.com/en_US/how-to-%7C-set-proper-gain-structure-in-q-sys)

These sources inform workflow and safety **principles**, not permission to use a vendor's proprietary DSP implementation or infer unknown speaker parameters.

## Actual native invariants

1. The accepted **ChannelId** post-master PCM renderer is the sole authority: Hardware IN / PLAYER -> channel FX -> fader/pan/mute -> WIRE/BUS -> MAIN -> master gain/mute/mono. There is no second physical callback, competing mixer or GUI-owned routing graph.
2. Speaker designs are revision- and device-epoch-fenced **muted drafts**. ASIO hardware pins remain disarmed; topology or device changes revoke prior acceptance.
3. CONTROL prepares crossover, EQ, delay, polarity, voltage-model coefficients and private scratch. AUDIO is bounded, no lock, no allocation, no device enumeration or settings computation.
4. M9 measured-voltage model requires DAC full-scale RMS, *linear voltage gain* for amplifier and driver manufacturer peak/continuous voltage data. It rejects invalid/nonfinite bounds. Optional safety high-pass is a prepared LR4. These are **declared test inputs**, not verified independent calibration.
5. M9 peak attenuation is **sample peak only**, not 4x/8x oversampled intersample true peak. Its RMS detector is exponentially weighted input power, not validated thermal, excursion or amplifier-headroom protection. Even a unit-passing digital limiter **must not** be advertised as safe speaker protection.
6. M10 diagnostic is solely an isolated memory-only output array. A new accepted muted draft may run at most **two device-rate seconds** of post-master PCM diagnostics (plus one callback quantum). Atomic CONTROL readback marks capturedFrames, captureFrameLimit, captureComplete and last measured per-way gain reduction / RMS / peak. Expired diagnostics do **not** keep consuming crossover CPU on every show callback. Readback says hardwareApplied=false.
7. GUI `/preview/` is the only approved design surface. Typed browser intent cannot set manufacturer calibration or ARM. Native exact DSP/threshold/pin readback must match draft before a *VALIDATED DRAFT* caption; it never says LIVE.

## Next P0 release sequence (independent gates)

### M11 — trusted field calibration + resilient session
- CONTROL-only source of manufacturer data and instrument-verified DAC voltage/amplifier voltage gain, with provenance, units, device identity and sample rate. Test calibrated model boundaries independently at the actual device. A UI checkbox or a pasted dBFS threshold can **never** turn an output on.
- Persist LMS draft in versioned `.smix`, including only stable logical section IDs, explicit device-identity bindings and disconnected state. Reopen, device reconnect, sample-rate/quantum change or unknown profile MUST reset to DISARMED with no auto patch.

### M12 — sole-authority hardware activation
- A separate, authenticated native physical output transaction must require current independently approved calibration, matching real ASIO callback pins, unoccupied driver output ordinals, exact accepted graph revision/device epoch, watchdog, runtime limiter state and explicit operator action. Every stale/missing condition fails silent.
- No second PCM engine: published output scratch and interleaving must be completed in the existing audio callback, with physical pin writer **exclusive ownership**. Never OR into MAIN/MONITOR or permit a source-only fallback to CH1.
- Produce actual callback ACK for each output pin; differentiate REQUESTED, ACCEPTED, APPLIED, MUTED, REVOKED. GUI may render LIVE **only after** matching native readback and independent hardware proof.

### M13 — physical FAT; required before enabling live speakers
- Test independent ASIO multi-output devices with dummy loads first (2/4/6/8 output ways, mono/stereo); test 44.1/48/96kHz, variable 32–512 frame callback quanta, graph retarget/remove, master mute and global fail-silent.
- Capture actual voltmeter/scope peak and RMS versus software modeled outputs. Sweep, limiter threshold/attack/release, hard clipping/inter-sample peaks, crossover phase, and per-way HPF using verified equipment. Independently verify external amplifier gain/clipping, driver thermal/peak limits, and polarity.
- Test late hot-plug, ASIO4ALL port reshuffle, sample-rate restart, stale epoch and buffer pointer invalidation. Any failure must revoke all speaker output rights immediately.
- Keep PR #635 draft and **no public speaker LIVE** until M11–M13 have signed hardware evidence; generic MSVC/CTest green is software-only, not a substitute.

## Efficient qualification
- Cheap native C++/Node and 1 MiB stack SonKu PCM preflight per relevant PR edit. Preserve `-Werror`, measured data and negative tests.
- Dispatch **one exact-SHA Windows native milestone** after cheap gates are green; add one full Chromium gate when UI/contract changes warrant it. Do not dispatch redundant FAT/installer on device/DSP-only drafts.
- Maintain one approved `/preview/` and no GUI rollback from parallel SonKu, Guitar FX, gain/SOF or Vocal Throw threads.
