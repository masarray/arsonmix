> **DEVICE SETUP INTEGRATION (2026-10-31):** This PATCH2 PRD governs routine SOURCE selection and patch behavior. For the separate *real ASIO/WASAPI device/driver/rate/buffer and enabled-pin configuration inside the existing matrix*, see [Custom Device PRD](PRD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md), [ARD](ARD_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md) and [handoff](HANDOFF_CUSTOM_AUDIO_DEVICE_SETUP_20261031.md). Native JUCE popup remains the current fallback; custom device APPLY is still **NOT IMPLEMENTED**.


# ArSonMiX — Professional Audio Source & Patch UX PRD (PATCH2)

**Revision:** 2026-10-31 | **Scope:** Windows LIVE digital mixer, not an audio editor | **Baseline audited:** main 498ded6678 | **Status:** PRODUCT REQUIREMENTS, not implemented or field-qualified.

> **Supersedes the operator workflow of PATCH1 #218.** PATCH1 was an incremental repair: it exposed per-channel input selection but still required MONO/STEREO → L/R → STAGE → MUTE → APPLY for a routine assignment. That is not the target workflow. The current safety gates stay in place until a native live-safe quick patch is implemented and qualified. This document does not declare one-click processing live.

## 1. Documented product benchmarks

**Waves eMotion LV1:**
- The selected Channel window INPUT section lists I/O organized by *device, then input channel*. Mono/stereo and grouped stereo pairs are displayed. A Channel may be patched right there; users do not have to enter a matrix for ordinary work.
- PATCH > INPUT is a filtered global matrix, with verified device sources at the top, mixer channels to the left, and direct clicks on intersections. Stereo L/R can be patched separately, one source can feed more than one channel. PATCH > OUTPUT, INTERNAL, DEVICE-TO-DEVICE and DELAY are distinct functions. A structural I/O patch can trigger a safety warning, which is configurable; this is NOT an unconditional global user-MUTE + per-row STAGE requirement.
- The mixer ROUTE mode also provides source assignment without leaving the mixer view. Do not copy pixels or claim SoundGrid support.

**PreSonus Studio One / current Fender Studio Pro:**
- Separate *hardware I/O pins* from reusable, named *software I/O channels*. Audio I/O Setup is for once-per-device/session configuration of mono/stereo input definitions and their hardware matrix.
- An operator subsequently selects the input directly in the Console channel's INPUT field. Studio Pro groups channel input and output selection at top of the strip. Reusable default/device-specific mappings reduce recurring setup.
- The Studio Pro manual explicitly allows physical I/O remapping to preserve Session/Show portability, import/export device configurations, and select from configured input sources. Its Audio I/O Setup has Apply for configuration; choosing an existing track/console input is a separate, short operation.

**Official evidence:**
- https://www.waves.com/emotion-lv1-tutorial-3-3-channel-window-inputs
- https://www.waves.com/emotion-lv1-tutorial-2-2-using-the-patch-window
- https://www.waves.com/emotion-lv1-tutorial-4-10-mixer-route-and-channel
- https://assets.wavescdn.com/user-guides/emotion-lv1-classic-console-user-guide.html
- https://fenderstudiopromanual.fender.com/en/Content/Setup_Topics/Audio_Device_Input_Output.htm
- https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm

## 2. Non-negotiable operator journeys

**A · Physical Input 1 → mixer CH3 (normal operating mode)**
1. The device is already configured and visible as an active ASIO/WASAPI capture source.
2. On CH3's existing strip / selected-channel INPUT field, click the current SOURCE label.
3. Choose **MAONO PS22 / Input 1 (Mono)**. The UI shows PENDING briefly and then the accepted named source / LIVE badge upon positive native ACK.

**Target:** maximum two intentional operator actions (open picker + choose source), *no separate setup dialog, STAGE, manual MUTE, APPLY or switch to another workspace for this routine action*. This is a design requirement of the NEXT milestone, not a claim about PATCH1.

**B · MAONO virtual stereo pair → mixer CH6**
1. In device setup, use an actual driver-reported stereo pair, or create a named logical stereo source **MAONO / Virtual L/R** using two enabled distinct capture pins.
2. CH6 INPUT picker → choose **MAONO / Virtual L/R (Stereo)**, one coherent choice. Native pending/ACK behavior same as A.
3. For unusual nonadjacent L/R ports, use once-only advanced source definition; do not require repeated two-pin selection per channel.

**Device truth:** the available sources depend on the exact PS22 variant, its routing center/driver and the selected JUCE device's enumerated capture pins. A separate Windows capture endpoint is NOT necessarily an additional pin in the active ASIO device. NEVER present phantom channels or promise arbitrary multi-device capture before validated clock synchronization. Official device references:
- https://faq.maono.com/docs/PS22-FAQs
- https://faq.maono.com/docs/ps22_live
- https://faq.maono.com/docs/PS22-Lite-OBS-Studio-Tutorial

**C · New hardware / driver changed**
Device Setup → select verified audio device/driver/rate/buffer → auto-load compatible saved source definitions. Display **REMAP REQUIRED** when an original source pin is missing. No silent fallback to a different port, no automatic gain/output unmute. Advanced PATCH may remap several sources at once, with REVIEW / APPLY and rollback.

**D · Global/mass patch**
Top PATCH → default to **INPUT / OUTPUT / INTERNAL** typed views with unequivocal FROM → TO direction, driver device filters, real pin labels and named channel rows. Click intersection for a source→destination. Optional range drag/batch assign only with collision preview, native validation, and undo. No duplicative independent patch managers.

## 3. Information architecture and visual standard

- **MIXER / ROUTE / selected channel:** INPUT SOURCE is always discoverable, compact and readable; one popover lists real saved sources grouped by Hardware, Driver Virtual/Loopback (only if native reported), and Internal Bus (only if real compiled route). Search for numerous sources; show device, mono/stereo icon, real port name and unavailable reason. Source label should update after ACK, not at click time.
- **AUDIO SETUP:** DEVICE (ASIO/WASAPI, sample rate, buffer, capture/playback capabilities), INPUT SOURCES (named mono/stereo aliases), OUTPUT SOURCES (logical bus output patch), ADVANCED MATRIX (actual physical pins), profile save/reload. The setup surface is not a maze of simultaneous fake crosspoints.
- **PATCH:** a professional global patching workspace, not another duplicate Audio Setup. Explicit INPUT, OUTPUT, INTERNAL and ADDITIONAL OUTPUT sections; only supported bus/device types should be interactive. Display distinct source→destination orientation; no buttons without native actions. Separate AUX sends, audio group summing, DCA control group, monitor and hardware outputs.
- **Safety and status:** IDLE / PENDING / LIVE (native acknowledged) / REJECTED / UNAVAILABLE / REMAP REQUIRED, concise reason and retry/revert. No success badge from local state alone. Put verbose epoch/revision diagnostics in a small optional technical inspector, not every mixer row.
- **Premium lightweight visual:** calm graphite/metal, crisp 10–12px typography for legibility, restrained cyan accent, no giant cards, fake hardware photos, excessive bold, gradients/blur/shadows on interactive hot paths, no per-pointer matrix recreation. Preserve L2 geometry, RAIL3 groove, pinned MASTER and original VST3 FX handling.

## 4. Safety principle: hide implementation complexity, NOT risk

A simple choice MUST NOT be made to appear one-click by publishing an unsafe graph while output is armed.
- **Current PATCH1 behavior:** staged full structural routing requires manually muted OUTPUT + APPLY + native ACK. It must remain truthful until a validated native quick-switch path exists.
- **Required PATCH2 native capability:** an input-only, same-device, single-channel quick patch with preflight/device-epoch revision checks, bounded preallocation and immutable prepared graph/route ownership. Publish at a safe audio-block boundary, keep previous graph on failure, and handle audio discontinuity with bounded de-click/crossfade or safe switch semantics. Do not construct/reload VST3, enumerate devices, allocate or lock audio thread. No automatic unmute of a user-muted output.
- If a change cannot be proven safe live (driver reopen, armed physical output reroute, channel/bus structural change), show one explicit confirmation or defer until muted; preserve current audio, without pretending it already succeeded.
- Native callback change belongs to the existing JUCE/DYN engine owner and must be reconciled with DYN1B-2B single-graph cutover. No second callback or parallel DSP graph.

## 5. Measurable acceptance / release gates

| ID | Scenario | PASS criteria |
|---|---|---|
| UX-01 | Verified Input1 → CH3 | Two operator interactions after device setup, clear source label, PENDING→native ACK→LIVE; no extra modal/Apply |
| UX-02 | MAONO virtual stereo → CH6 | One named stereo pair choice, signal L/R follows actual driver pins including nonadjacent aliases; no extra source-side edit |
| UX-03 | Driver virtual unavailable | No invented port or misleading success; clear variant/driver/recovery guidance |
| UX-04 | Existing live connection replaced | Old route survives negative/stale ACK, no user-muted auto-rearm; audio field trace shows no unexpected click/pop |
| UX-05 | Global patch matrix | One source-of-truth, explicit direction and category, supports real sparse pins and optional batch routing with undo |
| UX-06 | Portability | Show/profile preserves named logical sources; missing driver/pin triggers remap with stable identity |
| UX-07 | Meter and UX performance | No additional meter/pointer hotspot, 11-strip L2 and master alignment at 1024/1366/1440/1680, Windows iGPU |
| UX-08 | Native audio FAT | ASIO/WASAPI 44.1/48k buffer 64/128/256 where supported, physical CH3/CH6 stereo L/R, 0FX and loaded FX, xrun/crackle #15, hotplug/reject soak |

**Gate semantics:** Source/mock CI validates control flow only. A real MAONO PS22 and other Windows audio device FAT is required for audible routing/zero avoidable latency/reliability claims. No fake performance parity with LV1 SoundGrid.

**Acceptance priority:** UX-01 and UX-02 are P0 daily-operability goals. Implement together with safe native source-switching; avoid another cosmetic-only matrix milestone.

## 6. Delivery bookkeeping

- **Delivered at baseline:** PATCH1 #218 source-to-channel 10-row map, advanced matrix, native staged route, outputs and separate AUX; advanced matrix state persistence and mock acceptance. User-visible interaction still too long.
- **PATCH2 pending:** logical source registry & aliasing; strip-side source popover; safe single-channel native transactional quick-switch; one canonical global patch workspace; portable remap; field signal verification. No claims these are implemented.
- Track product epics #170, #179, #182, #192 and P0 audio issue #15. Do not create competing DSP milestones.

