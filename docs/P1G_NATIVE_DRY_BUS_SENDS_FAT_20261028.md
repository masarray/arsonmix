# P1F–P1G — Native 10-Channel Routing LAB + PRE/POST Sends (2026-10-28)

**Starting point:** reconciled latest `main@3b327bd5f9513ad405122258665457eb8ee07e73`. Preserve merged GUI finish, channel/master strip layout, button colors, master mono, CH1 native VST3/Orbit/FX reorder and bypass, backup, lean CI and physical no-FX diagnostic. The native Routing LAB existed already in main: this is an extension of that actual callback path, **not a second mixer engine or GUI mockup**.

## What actually runs natively now

- A verified direct JUCE device supplies **actual active hardware input/output masks**, not guessed pin ordinals; physical IN 3 can be callback[0], and a stereo pair can have nonadjacent physical pins. The P1F `JuceActivePortSnapshot` validates hardware topology at device start and records output mask/count. If the callback input **or output** count changes, LAB writes silence and never falls through to the live CH1 host.
- The opt-in `LiveDryRoutingLab` already called `PreparedMixerBlock` in `LiveAudioEngine` without changing the normal VST3 path. P1G now decodes one **transactional** routing command into ten physical/BUS channels and up to **32 internal PRE or POST fader sends**. Edits are validated completely off RT; a graph that contains feedback loops, duplicate edges, invalid port selection, invalid levels, self-send, stale device epoch or stale graph revision is **rejected without changing the active graph**.
- Channels can be `NONE`, physical `MONO`, physical `STEREO`, or virtual `BUS` (a native stereo return with no physical capture port). Each has a fader, MAIN output toggle, mute and solo. A source can go to MAIN, to sends only, or both. BUS returns can send onward to other buses in validated topological order. Native signal path: **real input → optional downmix/dual-mono → PRE send → fader/pan/mute/solo → POST send → BUS return → MAIN master**.
- The audio callback uses the existing preallocated array-backed `PreparedMixerBlock` and `RealtimeRoutePublisher`. Graph publication uses device-epoch and revision fencing. No JSON parsing, heap, I/O, plugin initialization, device restart, JS, mutex or thread spawn in the realtime render. Preserve 256-sample de-click ramp and strict emergency mute behavior.
- Ten true post-fader per-strip peaks are published using atomics (peak values reflect DSP channel output), exposed to the Routing LAB panel only; no fake VU or heavy blur/DOM animation. The GUI has one consolidated panel with physical input selectors, format/BUS, level, MAIN/MUTE/SOLO and an Add/Remove PRE/POST send list; it submits **draft only** and reflects the native ACK. The source list resets on device epoch change.
- One new JUCE C++ test validates command parsing, actual synthetic sample audio through pre-fader bus return with the source fader at zero, silence at post-fader sends, cycle rejection, invalid GUI values and rollback. An expanded Node/UI contract proves the source is connected to native audio and send controls. Existing P1A–P1F and P0 tests still run on the same Windows CI.

## Actual on-device usage — *controlled FAT, not a show build*

1. Open SIGNAL CHECK → **MUTE OUTPUT** first. Confirm DIRECT ASIO/WASAPI device is authorized. Remove any CH1 VST3 before using LAB.
2. Click **ROUTING LAB · 10 CH**. Select a real physical `MONO` or `STEREO` input for CH1, select `BUS` for CH2 or another return. Leave unused channels `NONE`.
3. For a dry AUX example: set CH1 `MAIN` off, CH1 fader zero, CH2 `BUS` with `MAIN` on. Press `＋ ADD SEND`, choose `CH1 → CH2`, `PRE`, gain `−12 dB`. APPLY TO NATIVE AUDIO while muted; verify ACK.
4. Unmute with **headphones and low external monitoring volume**, observe master and channel peak in LAB. The PRE send should still be audible even if CH1 fader is zero. Change send to `POST`, APPLY while muted; the output should be silent at fader zero. This is actual DSP, not an FX plugin reverb; BUS does not insert a VST.
5. Use `STOP LAB` to exit back to the original CH1 host. Output stays muted for safety. If the device restarts, the LAB route becomes stale; inputs need explicit remapping and fresh native approval. Invalid sends produce a reason and preserve the prior verified state.

## Explicit uncompleted qualifications

- **No real VST3 chains on CH2–10.** No AUX hardware outputs or true plug-in FX returns yet. BUS is internal dry mixing, not an FX plugin. CH1 native VST3 remains suspended/excluded during LAB and resumes only after STOP.
- The production JUCE host initialises with 1 input and 2 outputs. Extra active ASIO hardware inputs require the user to enable channels in AUDIO SETUP; the LAB cannot silently reopen or activate absent ports.
- The new status outputPort mask is backend-reported. Hardware port names, device serial identity, actual dual-clock endpoint identity, PDC and portable per-user routing persistence remain follow-on obligations.
- Real hardware click/dropout validation, independent-clocks FAT, VST3 bus negotiation, latency compensation and **Issue #15 zero-FX crackle** are still not qualified. CTest PASS only proves programmatic contracts; never label a build FAT-certified based on CI.
- Bounded 32 sends, ten strips and stereo MAIN are an implementation scope; these do not limit the flexibility of imported VST3 plugins or public/private feature parity.

## Merge gates and follow-on

Use exact-head Windows MSVC, CTest and JS+Chromium checks, compare main before merge, and verify GitHub release artifact. GUI finishes currently in main **must not be overwritten**. Next milestone: native CH1–10 independent plugin instance ownership, stereo/mono VST3 bus negotiation and latency/PDC, publish/retire all FX state safely off callback; activate normal GUI SOURCE/FX only after real hardware acceptance.
