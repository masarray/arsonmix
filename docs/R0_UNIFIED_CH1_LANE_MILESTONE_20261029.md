# R0A — Execute existing CH1 vendor FX inside 10CH native routing render

Baseline `81c892f39996689c63b5704dcc060a9c42f0339d`; owner production mandate: [#143](https://github.com/masarray/arsonmix/issues/143).

## Delivered (native integration, not a cosmetic LAB rename)

- `LiveChannelFxGraph` now has a strictly optional, callback-scoped, non-owning **CH1 external insert lane**. The host supplies its existing `LiveGraphHandoff::Cycle` lease while rendering native 10CH+4AUX: no second output mix, no second host, no VST instance copy, no new realtime allocation/lock.
- The CH1 multi-insert rack is run in the same CH1 PRE/POST send/master topology, with exactly the original processor addresses and Orbit parameter ownership. Pre-existing vendor settings/tails are not reinitialized on route apply. Existing per-insert bypass envelopes execute in this lane.
- Native apply refuses a duplicate native CH1 insert when the old rack is present; additionally rejects a non-zero-latency CH1 rack until PDC exists. Existing physical device/epoch, muted structural APPLY, graph revision, no active independent clock and real-port gates remain authoritative.
- The old `LiveDryRoutingLab` / native UI action names remain as **internal compatibility** at this stage. Preserving controls and safety until R0B/R1 avoids presenting a broken main-window GUI.
- New core regression asserts genuine CH1 external DSP inside a prepared 10-channel graph, one invocation per audio block, no RT allocation, no duplicate plugin instance, dry fallback identity. Existing graph and UI tests stay intact.

## Still outstanding before R0 is DONE

- **R0B production activation:** replace two mutually exclusive control workflows with a single startup native mixer session, and migrate/orchestrate CH1 FX ADD/REMOVE/MOVE/preset/Orbit operations without detached callback or device epoch invalidation. Current CH1 structural edit while routing active is explicitly blocked; do not claim full performance-mode lifecycle or remove STOP yet. This is a safety gate, not a cosmetic UI milestone.
- Do not show unassigned CH2..10 as live; connect physically via ACK. Product-level ROUTING/PATCH UI (matrix) follows as R1 after native R0B. The current LAB name persists in old UI until the real operational split is removed; no dishonest label-only change.
- Hardware #15 crackle, proper PDC, vendor crash isolation, continuous scene/VST3 state recall, physical output patches and full hardware FAT are separate gates.

## Validation

The extended `ARMixLiveChannelFxTests` executes a simulated real JUCE channel processor and verifies no allocation on the native callback loop; Windows MSVC CTest and Chromium UI smoke should be required on this PR before merge. CI alone does **not** validate audio hardware.
