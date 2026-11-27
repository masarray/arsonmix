# SonKu full-wet / Head Yaw — owner correction 2026-11-27

SonKu is a serial binaural insert. Valid legacy mixes migrate to 1.0 on native admission/restore, preserving SONK versions and offsets; invalid settings remain rejected. The pinned adapter variant changes SonKu-only queue/restore policy and removes full-output fade for same-mode P90 yaw. Derived ImmerSIB retains former mix/fade. Other mode switches keep the original fade. Historical approved manifest and acoustic headers unchanged. No bypass removal or change to internal bass/side transfer.

The existing P90 FIR target slew is reused; no new renderer or allocation. The UI uses the bounded live knob publisher with ACK/readback and disables yaw on unsupported legacy mode 0. Native, Chromium and physical headphone/ASIO gates are distinct. Prior P11 acoustic candidate remains separate. No promotion until exact current checks pass.
