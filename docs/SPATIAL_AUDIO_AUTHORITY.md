# Spatial audio ownership / qualification — single source of truth

**Public product name: ImmerSIB.** Former visible label: ImerSip. JUCE processor name, editor title, FX Picker and user-facing errors must use **ImmerSIB**. Preserve the original `armix.builtin.imersip.v1` catalog identity, IMRS state, C++ identifiers, asset paths and existing `.smix` sessions for backwards compatibility.

**Scope (2026-11-16):** `ArSonMix` experimental spatial FX only. This document explains the machine-readable [research authority ledger](../config/spatial_audio_authority.json). The ledger is **not runtime configuration** and must never become a second audio DSP state or be used to silently migrate a user's `.smix`.

## Which one do we develop?

| Role | Authoritative reference | Safe decision |
| --- | --- | --- |
| Confirmed P9.1+P9.2 sound baseline | [#719](https://github.com/masarray/arsonmix/pull/719); frozen [issue #720](https://github.com/masarray/arsonmix/issues/720), SHA `763706a15a2617a10d23b5e2e43eb5689e454ccb` | **Keep immutable**, compare every acoustic change with it |
| Active ImerSip integration | [#722](https://github.com/masarray/arsonmix/pull/722) stacked on #719 | Only ImerSip development direction; same P9 core |
| Old ImerSip P8 DSP | [#706](https://github.com/masarray/arsonmix/pull/706) | SUPERSEDED; retain branch only for historical A/B and IMRS v1-v5 restoration |
| Divergent P9.1/P9.2 trial | [#718](https://github.com/masarray/arsonmix/pull/718) | **NOT** identical to #719; unselected alternative, not to be merged by accident |
| P9.0 | [#717](https://github.com/masarray/arsonmix/pull/717) | Required ancestor of #719, not the P9.1/P9.2 sound reference |
| Legacy P8.3 | `sonku-p83-native-speaker-gates.yml` | Rejected source-quality candidate. Preserve its strict red evidence, but invoke only from P83-specific PRs or deliberately via manual dispatch |

**Important naming clarification:** `SonKuSpatialCore` uses **`renderMode=9` and internal enum `MusicSpatialP90`** for the reconciled P9.1/P9.2 **algorithm**. That numeric mode is retained for SONK v11 and `.smix` compatibility. It **does not mean** the renderer reverted to P9.0. The P9.1/P9.2 world-bass foundation is inside that same compiled mode. Never invent mode 10, second convolvers, duplicate stereo matrix, or a separate P9-only audio callback just to make a marketing version label match.

## Sound and factory defaults must not be conflated

- **New ImerSip insert factory** (`Live Concert`): static FRONT, stage **150°**, distance **2.2 m**, Large 28.8m, EARLY **100%**, LATE **91%**, MIX **100%**, OUTPUT **+4.8 dB**. This is a user-feedback-led audition **candidate**, not guaranteed loudness or perceptual validation.
- **Neutral A/B is removed from the UI entirely**, as requested. The exact original reference remains an **internal test function only** (`imersipP92ReferenceScene()`) for PCM regression, not a user-facing preset, not a hidden selectable item.
- User's **original ImerSip screenshot** remains recorded as evidence: 150°, 1.6m, ER 100%, late 91%, +4.1 dB. It is *not* the older independent #720 SonKu screenshot. The revised `Front Row Power` id 5 intentionally strengthens this close-stage idea (ER 100%, Late 97%, +5.2 dB).
- Revised owner-facing choices: **Live Concert** (positive baseline unchanged), **Royal Concert Hall**, **Arena Thunder**, **Front Row Power**, **Symphonic Palace**, and **Velvet Cinema**. These use one native P9.2 DSP with different direct distance, stage width, early vs late room and gain; descriptions reflect audition intent, not measured acoustic hall certification.
- [Operator's frozen P9 checkpoint #720](https://github.com/masarray/arsonmix/issues/720): screenshot with **82°, 1.6m, Large, reflections 100%, reverb 62%, output +7 dB and revolution time 8.5s**. These are owner test values **not accepted factory defaults**. The moving toggle / static status was not established from the revolution-time readout alone.
- Changing factory presets only affects **new, empty insert instances**. IMRS v6 accepted state and old v1-v5 migration remain unchanged; a saved Custom 150° preset must not be overwritten on reopen.
- A physical speaker farther away changes direct-to-reverberant balance; it does **not automatically eliminate phase cancellation**. Presets use 2.2–3.0 m for audition, maintain independent L/R HRTF and retain the source anti-phase rejection guard.
- The user wants a perceptibly louder, enveloping and rich effect: the output gain candidates are not a blind automatic loudness guarantee. The existing linked 0.98f peak guard protects sample peaks; mastered near-0 dBFS tracks may trigger reduction/compression. Compare actual levels with BS.1770 LUFS and true peak where available, and inspect limiting/noise at native PCM.
- Never overwrite a user's accepted settings with any of these on session restoration. Compare at **identical native scene and matched actual output loudness**, not just equal knob labels.

## Frozen acoustic source fingerprints

The ledger pins the **Git blob SHA-1** of seven source files making up the validated P9 acoustic transfer, including the measured HRTF front end, source-to-ear stereo decoder, world-positioned bass, early reflections, and late room. The independent Python contract recomputes each blob from checkout bytes. Any unintended change fails fast **even if `renderMode=9` and all interface names remain unchanged**. An intentional sound update requires a newly recorded operator-approved checkpoint and corresponding ledger review; **do not edit expected hashes merely to turn CI green**. These are source-provenance checks, not a replacement for headphone listening or a claim of identical compiler output on every platform.

Only native/engine edits trigger the expensive ImerSip Windows FAT; documentation/authority changes use the separate fast `spatial-audio-authority-fast.yml` contract instead.

## Ownership boundaries and CI classification

1. **Audio truth:** `SonKuSpatialCore` compiled with explicit isolated P9 research flags. ImerSip is a distinct `armix.builtin.imersip.v1` FX identity but inherits the same `SonKuSpatialProcessor` graph, not the previous `ImerSipDsp`. No unapproved DSP layering.
2. **Persistence truth:** `IMRS v6` wraps real `SONK v11` accepted state. Old IMRS schemas are deliberately mapped to P9 parameters, not acoustically lossless. Invalid v6 must be rejected *before* mutation. GUI effects apply only after real PCM ACK and per-slot host Power uses actual host bypass.
3. **Qualification truth:** PR #722's focused native source PCM, actual Windows JUCE CTest, GUI EXE and package verifier are the relevant **engineering acceptance checks**; the old P8.3 check is an **archived, known-rejected acoustic reference** and is **not** the P9/ImerSip release gate. No strict assertion is removed from P8.3; it still executes on explicitly named P83 PR branches and via manual workflow dispatch.
4. **Release truth:** A green source/Windows CI run does **not** mean perceptual qualification, ASIO hardware FAT, headphone preference parity or public release. Keep PR #722 draft and production `main` untouched until explicit decision.

## Future safe workflow

Start from the **immutable checkpoint** for acoustic experiments; use #719 lineage for ImerSip. Run source identity/PCM parity before expensive Windows, then exact-commit native JUCE/state/host-bypass and approved GUI/distribution gates. Require user headphone A/B if altering P9 DSP. For P83-specific research, use a head branch containing `p83` or manually dispatch the archived P83 workflow. Do not delete its recorded rejection or turn an objectively failed P83 sound experiment into an artificial CI success.

## ImmerSIB P9.7 + P9.8 scene tuning — owner-directed candidate (16 Nov 2026)

- **Never replace the accepted Venue**: preset ID 2 is P9.7 Venue Mega Space at 10m / 106° / full early and late room / output trim 1.8, with voicing ID 0; all preexisting P9.7 and P9.8 voicing curves 0–3 remain **bit-identical**.
- **Additive P9.8 engine extension**: native voicing ID 4 (`ImmerSIBDetail`) raises the measured-speaker HRIR center presence gently, improves vocal/foreground midband articulation and enhances real directional early-reflection air. ID 5 (`ImmerSIBImpact`) is tuned for physical LF/kick body and stage, within the same world-speaker graph. IDs 4–5 are private candidate *scene parameter profiles*, not extracted instruments/stems. No crossfeed/delay or dry-stereo widening is added.
- Royal Concert Hall, Front Row Power, and Symphonic Palace use ID 4; Arena Thunder ID 5; Velvet Cinema existing Cinema ID 3. Exact preset geometry and gain remain unchanged to isolate voicing effects.
- **Deliberate research hash change**, not accidental DSP drift: `SonKuP98SceneVoicing.h` originally Git blob `78d8ff28aaf9115a0d1cfff99a6cbb08d0b2bf39`; additive extension blob `5b2438d9b743612c0d22a9b7f9984b1a0289ee3a`. Both hashes are recorded in the authority ledger. The P9.7 Venue golden PCM must remain bit-exact at 44.1/48k and the existing 0–3 voicing trajectories remain identical.
- **State compatibility**: the existing serialized SONK v12 / IMRS v7 32-bit voicing field carries IDs 4–5 without format change; IMRS v6 migrates to neutral 0; stored `armix.builtin.imersip.v1` FX identity remains unchanged.
- **Acceptance boundaries**: measured PCM fidelity, finite peaks, anti-phase survival, room/side energy, and Windows JUCE tests qualify a candidate, not proof of isolated instrument stems or owner-approved subjective enjoyment. Physical headphones, matched loudness, and mastered content are mandatory before release.

## ImmerSIB nearfield vocal priority — 2026-11-16 (owner feedback, CANDIDATE)

- The operator reported previous 4.5–10m presets as too vague. The public P9.8 factory/GUI now uses **1–4m** only (Studio 7.2m has physical geometry cap 3.05m). Former **10m P9.7 Venue** remains an immutable private audio regression fixture (`imersipP97VenueGoldenScene()`); the public Live Concert is **deliberately revoiced** at 3m and is NOT falsely described as the old Venue PCM.
- Early Room is **0–180%** only for native mode 9. From 0–100%, existing wall gains and PCM are unchanged. >100% physical wall-material tap value stays clamped at 100%, while the bounded independent early-reflection presence factor continues increasing. Late Room remains separately 0–100%; raising early is NOT a separate reverb feedback loop and does not create new reflection times.
- `SonKuP98SceneVoicing` profiles 4 (Detail) and 5 (Impact) now add two stable fixed-biquad SIDE-presence filters and a single-pole SIDE-high-air spectral shape **BEFORE** the original two world-speaker KEMAR HRIRs. This offers restrained frequency-specific masking relief for center-dominant vocals vs lateral material; it **cannot identify vocals, recover multitrack stems, or guarantee source separation**. Fixed coefficients, smoothly interpolated gains (55ms), no allocations, per-sample transcendentals, extra convolution or stereo Haas delay.
- Existing voicing 0–3 remain identical; the original P9.7 Venue PCM on 0..100% Early should stay byte-exact at 44.1/48k. Changed Git blob hashes are explicitly pinned as an intentional new audition milestone. Other P9.7/P9.8 code paths unchanged.
- `IMRS v6/v7` old settings migrate far source distances down to physical <=4m on restore; invalid modes/voicings are still rejected atomically. `.smix` FX catalog identity unchanged.
- Verify headphone listening at **matched level**, center-vocal clarity vs cymbal presence, stereo side retention, mono fold-down, sibilance, and guard action with mastered songs before release. This is an experimental candidate, not an approved listening checkpoint.
