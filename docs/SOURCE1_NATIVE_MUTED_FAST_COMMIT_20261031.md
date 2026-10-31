# SOURCE1 — Native Hardware Input to Mixer Channel

**31 Oct 2026 | Branch:** feat/hardware-source-operator-m1-20261031 | **Base main:** e11a9799 (M1 AUX realtime). This combines the once-unmergeable SETUP1 #222 real Windows device picker/64-pin enablement and PATCH2's muted direct channel source transaction on the same existing native routing controller.

## Implemented operator flow
1. AUDIO DEVICE -> DEVICE / DRIVER -> JUCE native selector: configure ASIO / Windows Audio (WASAPI) / DirectSound only when actually enumerated. Choose input pins, output pins, sample rate and buffer. The 64-pin selector is a cap, not guaranteed hardware ports, multi-device capture or 64 mixer strips.
2. MIX -> ROUTE -> CH SOURCE -> select driver-verified mono/stereo source. If output is already MUTED, has no unrelated staged changes and device epoch is valid, a SINGLE existing routingDryApply request is submitted immediately. UI reads PENDING until positive arMixRoutingLabAck, never announces LIVE from a browser selection. Negative ACK restores old accepted route. No second graph/control plane.
3. If output is ARMED, fail closed, do not auto-MUTE/unmute, retain old accepted route and show explicit instruction. This is **not yet** live seamless source switching. Advanced nonadjacent L/R is configured in existing matrix with safe MUTE/APPLY.
4. Top PATCH -> real CH to AUX 1-4 PRE/POST sends -> internal FX returns -> MAIN; independent MONITOR 3/4 only with actual output pins. GROUP, DCA, MATRIX, CUE not implemented.

## Validation and remaining release gates
Node source contracts, Chromium mocked routing acceptance/negative armed gate, native Windows MSVC/CTest and package checks are necessary; physical ASIO/WASAPI/PS22 capture pins, audio stability and #15 0FX crackle need actual operator FAT before declaring performance equivalent to eMotion LV1 or Studio Pro. No invented PS22 virtual pins or global cross-clock capture. Saved named logical source registry, quick *armed* source crossfade/declick and portable device remap remain PATCH2 pending. One prepared native renderer, existing VST/Show leases, MAIN/MONITOR/OUTPUT1, M1 realtime sends-on-faders and L2/RAIL3 preserved.
