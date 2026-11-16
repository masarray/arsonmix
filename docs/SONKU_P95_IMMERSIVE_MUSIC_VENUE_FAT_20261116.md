# SonKu P9.5 Immersive Music & Venue — acceptance and architecture (2026-11-16)

**PRIVATE DRAFT: no public release, no claim of dearVR acoustic parity.**

## Operator sound reference
The operator likes P9.1/P9.2 **music fidelity, real separation and living external sound** but wants deeper front/back and a much larger sound-system presentation, with powerful yet controlled bass. Reference screenshot settings: P9.4 mode, 6.5 s orbit, 82° spread, MIX 100%, GAIN +7dB, LARGE room 28.8×28.8m, distance 3.1m, reflections 100%, reverb 62%, plus rear/front distance concern. This is the starting reference, **not** a verified universal optimum.

## Protected checkpoints
- Operator-accepted P9.1/P9.2: `763706a15a2617a10d23b5e2e43eb5689e454ccb` checkpoint issue #720
- GUI-only PR #721 retains 148×83 *visual perspective ellipse* as the correct 3D view.
- P9.4 PR #723 provides real measured KEMAR source with bounded median-plane DRR adjustment; no claim of measured individual BRIR.
- P9.5 is an isolated child branch. No merge into main without headphone FAT.

## P9.5 implemented engineering
1. **Bypass animation** — the JS visual PLL stops integrating when accepted native FX state is OFF / RACK OFF / UNKNOWN. Discard stale observed azimuth on disable. Do not reset to 0 on every 250ms readback. Resume only from current native telemetry; freeze visual pose while audio callback bypassed. Native FX rack remains sole audio authority.
2. **One latest-engine selector** — no confusing P8.4/P8.9/P9.0/P9.4 research catalog for a P9.5-capable build; legacy saved mode can be displayed honestly as legacy session and switched to latest by explicit operator action, never silently rewritten.
3. **Factory scenes** — five deliberate starting points, full native setting transaction + ACK/readback, editable afterwards: Signature 3.1m/100% ER/62% late/+5.6dB (1.9x linear), Large Studio 4.8m/94% ER/72% late/+4.3dB, Cinema 7.5m/100% ER/84% late/+4.6dB, Venue 10m/100% ER/100% late/+5.1dB, Live Orbit 6.5m/100% ER/83% late/+4.9dB. All use the physical LARGE 28.8m room model, **not claimed as distinct new BRIR measurements or individually different architectural room designs**. Old SMALL/CHAMBER values remain in the schema for session compatibility.
4. **Default only for NEW P9.5 inserts** — initialize `accepted_` with Signature rather than 1.6m/ROOM OFF/outputTrim .75. `.smix` restore applies saved exact settings. Other experimental/production builds unchanged.
5. **Slow electrical music-presence compensation (opt-in)** — 280ms stereo energy detector, 450ms smoothing of the makeup, update desired max once per 256 PCM samples, upward-only target 1.0..1.65. A near-1dB electrical minimum is a **target, not guaranteed loudness** with strong transients. Never reduce explicitly requested positive GAIN. Existing linked 0.98 sample peak limiter remains, peak safety comes before any claiming of dynamic improvement. P9.5 only, no WebAudio, heap, second graph, or front/rear geometry changes. Force bypass at 0% mix.
6. **Large venue low-frequency foundation** — existing three finite-Q room modes increase by at most 18% on the ROOM=Large wet modal return, *not* on direct bass; no fake subwoofer box or hidden low shelf. Bass/treble integrity and attack must survive field tests.
7. **No new .smix schema**: mode9 and typed native settings remain the same, new feature advertised only by `p95MusicalPresence` readback. Existing saved state is authoritative.

## FAT / reject
- Linux source P9, P9.1/P9.2, P9.4 baseline PCM checks, P9.5 44.1/48k and stereo anti-phase, 64/256 buffer parity; UI Chromium with bypass motion freeze and 5 scene transaction/readback.
- Windows actual JUCE + V11 state ACK, GUI portable, checks / source provenance. No release until operator listens.
- Critical loudness: equal-gain A/B and user-preferred +gain A/B are **separate**. Compare integrated loudness, short-term crest factor and limiter activity; do NOT select louder just because it wins unblinded A/B.
- Headphones, same device / gain path / speaker fader / source, actual 0/90/180/270 and 6–12 second orbit, 3.1m/7.5m/10m; compare bass drum punch, 50–120Hz notes, cymbal air and stereo music separation. Retain P9.1/P9.2 baseline for quick revert. Reject if boost causes pumping, peak flatlining, harshness, mono collapse, spurious echoes, or ASIO interruptions.
- Important known ceiling: these five named scenes still share existing native large-room geometry and algorithm. True physically distinct cinema/venue BRIRs need new early-reflection geometry, materials, speaker directivity, and listening measurements. Label this as a FOUNDATION, not completed dearVR equivalent.
