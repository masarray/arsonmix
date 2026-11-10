# ArSonMix — Harmonice Duo, Doubler & Shared Built-in FX User Presets
**2026-11-11 · Owner requirements / actionable implementation blueprint · NOT YET IMPLEMENTED**

## Context and required reconciliation
Audit: main moved from `a07b16e1c6f04473d328ddd1deab607774375ee3` to `a05485e5aa6682dbc196ce99ee50763eebd1dc19`. Read live head before code. [Harmonice P0 draft #529](https://github.com/masarray/arsonmix/pull/529) already works on natural stereo lead Pitch Tune, register-aware ensemble and premium small switch. Do not overwrite its files; add minimal 2-button change through that owner or after integration. [Latency experiment #521](https://github.com/masarray/arsonmix/pull/521) is research-only; retain established 1536 sample/32ms 48k DSP PDC until separately proven safe. Follow `docs/architecture/FEATURE_EXTENSION_PLAYBOOK.md` and `docs/architecture/MAINTAINABILITY_EXECUTION_V1_20261111.md`.

## Owner-reported bugs
1. Harmonice VOICES GUI only offers **3 / 4 / 5**, although native `Settings::voices` accepts **2–5** and factory `Natural Duo` preset sets `voices:2`. **Expose 2 as an equal first-class option.**
2. There is no simple unison vocal **Doubler** (BOSS VE-like double-track sound). **Doubler must NOT be mistaken for Duo:** Duo = 2 pitched harmony voices plus original lead; Doubler = a synthetic additional same-melody performance with controlled natural microtiming, timbre and detune.
3. All built-in FX (**Parametric EQ, Compressor, Harmonice, ArSon Vocal Throw**, future native FX) need persistent **user-created presets** independent of a session; a 'Custom' label or factory preset selector alone is insufficient.
4. Existing robotic Pitch Tune and raw checkbox remain urgent separate P0 issues. Current draft #529 addresses them; after tests, correction must be transparent/natural, compact toggle theme-matched; selecting a voice preset must not silently turn Pitch Tune on.

## Verified repo seams
- `site/preview-v2/assets/mixer-native-harmony.js`: only 3,4,5 buttons, factory Duo `voices:2`, valid range 2..5; native editor uses queued control + ACK/readback.
- `engine/builtin_fx/HarmonyDsp.h`: pitched wet backing shifters and spatial room, but no distinct unison Doubler mode.
- `site/preview-v2/assets/mixer-native-compressor.js` and `mixer-native-vocal-throw.js`: fixed Factory presets / 'CUSTOM' display; no persistent user library. `mixer-native-peq.js`: editable EQ without shared preset bar.
- `engine/builtin_fx/Channel{Eq,Compressor,Harmony}Processor.h`, `VocalThrowProcessor.h`: separate native parameter/state implementations. `restoreUnpublishedState` is for an unpublished processor, NOT automatic live preset recall.
- Existing `src/NativePresetArchive.h` / `docs/P2_NATIVE_PRESETS.md` `.armix` archives represent My Sounds/whole multi-insert chain including vendor state, NOT a per-built-in-FX preset. `.smix` is a mixer session. Avoid conflating all three.

## Professional UX and acoustic references
- [FabFilter Pro-Q 4 saving presets](https://www.fabfilter.com/help/pro-q/presets/savingpresets) + [loading presets](https://www.fabfilter.com/help/pro-q/presets/loadingpresets): header preset name, Save As, factory/user organization, confirmation before user overwrite, dirty state. Adopt behaviors not copyrighted artwork.
- [iZotope Nectar 3 preset window](https://s3.amazonaws.com/izotopedownloads/docs/nectar300/en/presets/index.html): factory vs Custom and Working Settings.
- [Valhalla DSP preset menu](https://valhalladsp.com/2012/03/12/valhallaubermod-the-preset-menu/): lightweight dropdown/Save As, portable copy. Advanced clipboard/favorites optional.
- [BOSS VE-20 Double/Harmony](https://www.manualslib.com/manual/141723/Boss-Vocal-Performer-Ve-20.html?page=18): Double and Unison are distinct from pitched harmonization. Adapt clear product semantics.

## P0: Direct 2-voice selector
- VOICES `[2] [3] [4] [5]`, equal size, compact, keyboard accessible, selected state follows accepted native DSP readback, not speculative click. Tooltip "2 harmony voices + lead".
- `Natural Duo` must visibly select 2; selecting 2 must send real `voices=2` and highlight after matching instance/revision DSP ACK+snapshot. Native editor reopen, `.smix` recall, device reconnect and switching 2→5→2 agree.
- No implicit Pitch Tune enable on Duo, 2/3/4/5 or other preset. Do not change legacy voice count semantics. Add Chromium desktop/compact screenshots and direct button/preset/ACK tests.
- PR #529 owns the same GUI file: do not produce two competing edits.

## P1: Real Doubler engine and UX
- One explicit, versioned `HarmonyMode::Harmony | Doubler` (name adjustable) carried by DSP settings, state serialization/migration, native GUI control and accepted readback. **Never use voices=1 as a fake flag or reinterpret existing 2 as doubling.** Old saved states map to Harmony with original voice count.
- Doubler generates a parallel unison take of lead F0/melody, with small deterministic phrase-aware timing deviations, subtle cent variation and individualized spectral contour, modest stereo/room. Avoid one fixed slap delay, extreme flanging, artificial note hops, constant over-vibrato or vocal muddiness.
- Compact UX: `DOUBLER | HARMONY` segmented choice or explicit `Double Light` / `Double Wide` real audio presets next to existing controls. Relevance of Key/Music should be truthful; Doubler does not require Music key and cannot secretly activate lead Pitch Tune.
- Harmony retains 2–5 pitched wet voices; `Natural Duo` remains two harmonic backing parts, not unison. Shared Space/Wet/Color/Gain, delayed original lead, existing rack bypass and PDC.
- Tests: dry/Wet0 exact lead, F0 of unison double vs pitched duo distinct, mono phase and stereo stability, 32–192k rate, callbacks 64/128/256, device restart, v1/v2/updated-schema migration, CPU and real-singer loudness-matched A/B. Naturalness is **not** verified by synthetic PCM.

## P1: Shared professional preset bar for ALL native built-in FX
Compact conceptual header, adjusted to each editor width (not a bulky popup):
```
[ Factory: Vocal Warm ▼ * ] [‹] [›] [Save ▾] [FX Power] [Close]
Menu: Factory / My Presets / Save As... / Save / Rename / Delete
```
- One reusable `BuiltInFxPresetBar` UX/controller, with per-editor styling only; no four bespoke preset systems and no oversized fonts/cards.
- Factory presets immutable; user can start from them and `Save As` a new named user preset. Applied changes show `Modified` or *; never silently overwrite Factory.
- Save As: small anchored floating name editor, current suggested name preselected, optional category, Save/Cancel, Enter/Esc and focus restoration. Duplicate same effect/category/name gets explicit conflict handling. Save overwrites a user-owned preset only with suitable confirmation; Save As always creates new ID.
- My Presets browsable in same dropdown, category grouping, sort stable, selected name, optional prev/next; on cramped editor hide prev/next before hiding Save As. Rename/delete only users and confirm. Recall dirty working sound with transparent unsaved-change warning or lightweight working settings/undo. No fake SAVE while DSP pending.
- Native apply shows temporary honest pending status; persistent save only confirms after atomic disk commit, preset recall only confirms after exact DSP-applied ACK plus readback. Browser preview cannot pretend there is native audio.
- Avoid chrome context menu, overflow, drag conflicts; preserve compact theme and accessible keyboard interactions.

## Single durable native preset library (not localStorage)
- Distinguish `.smix` mixer session, `.armix` My Sounds/FX chain, and a **separate per-FX preset format** (candidate versioned `*.armixfx`; choose exact format after auditing existing codecs).
- A single CONTROL/worker-owned `BuiltInFxPresetRepository` (name tentative) stores user presets per `catalogId`. Stable `presetId` survives rename; record owner Factory/User, name, category, validated schemaVersion+typed parameters, timestamp. No immutable preset identity based on channel, slot, device or GUI label.
- Use installed/portable workspace paths per `src/PortableWorkspace.h`; never put user-owned presets inside public repo or bundled release. Atomic temp-file + same-volume rename, size/name limits, safe Unicode, no path traversal/symlink escape, corruption/unsupported version fail closed, duplicate collision handling, safe backups.
- One shared native contract `list/saveAs/save/recall/rename/delete` with requestId, catalogId, presetId, stable FX instanceId, channelId and current graph/document/device/library revisions. List/library actions cannot mutate DSP unexpectedly. Stale/reordered/removed instance requests get negative ACK.
- **Save:** capture truly accepted, last-applied DSP parameters for exact FX instance after outstanding edits settle. No unsafe calls to realtime-owned `getStateInformation`. Disk save on worker/control and only then success.
- **Recall:** worker parse/validate+migrate, control thread precompute immutable parameters, send to existing FX SPSC, native apply and accepted snapshot readback confirm. Never set unpublished-only state directly on a published processor, never file IO/parse/alloc/lock in audio callback. Parameter-only recall should not rebuild graph or force MUTE/MONITOR/ARM/APPLY.
- Adapter/codec per effect: EQ band state; Compressor threshold/ratio/envelope/mix; Harmonice voice count/mode/key/wet/space/color/gain plus explicit correction; Vocal Throw delay/trigger/ducking/manual tempo. No broad Main.cpp switch proliferation; align with maintainability host-seam plan.
- **Portable vs session-bound state:** A Harmony preset may contain manual key/scale and a Music mode preference, but must not embed another session's arbitrary `guideChannelId` as a globally portable binding. Similarly do not persist Vocal Throw live Music ChannelId, detected BPM, throw audio buffers or live meter. Use safe Manual fallback; session-only bindings belong to `.smix`.
- **Excluded:** physical I/O, channel fader/mute/solo, WIRE, FX Rack power/slot bypass, VST3 vendor states, live audio history and global mixer session. External VST3's own preset UI remains separate.

## Milestones / release gates
**M0:** audit live main, #529, actual DSP codecs and portability; clarify exact bridge/owner, no duplicate host.
**M1 P0:** visible 2 button, Duo parity, actual GUI/native tests/screenshots, coordinated with #529.
**M2 P1:** authentic Doubler DSP mode + migrations + Double Light/Wide presets, PCM tests, A/B stems.
**M3 P1:** shared native preset repository and reusable compact preset bar, prove true disk Save As / restart / another channel / audio recall on one FX.
**M4 P1:** same proven service/UX integrated into EQ, Compressor, Harmonice, Vocal Throw, with individual adapters and no duplication.
**M5:** selective exact-SHA native/Chromium tests, portable roundtrip, corrupt/disk-full/read-only failure, stale ACK, session/device/reorder, physical Windows ASIO singer and CPU/XRUN qualification.

**Acceptance matrix:**
1. VOICES 2 and Duo both render exact native `voices=2`; 2/3/4/5 parity survives restart.
2. Doubler unison target vs Duo pitched targets are **audibly different and musically correct** with unchanged lead when Pitch Tune OFF.
3. Every built-in: Save As → change parameters → recall → audible DSP change → cold reopen → My Presets remains → recall same preset on a second channel.
4. Factory is immutable; user Save/Rename/Delete/CANCEL work; dirty state truthful.
5. Corrupt/missing/duplicate preset, incompatible plugin, disk-full, stale device epoch, changed FX slot leave audio/session unmodified and report an actionable failure.
6. Full `.smix` session and legacy `.armix` chain unchanged; presets survive device rebind/portable move; no second PCM authority/RT work.
7. Do not claim physical ASIO or real-singer naturalness from a green CI test. Measure callback p95/p99/XRUN and listening A/B separately.

**Execution instruction:** This is a repository-backed implementation plan, NOT completed feature code. Next engineering thread must reconcile current #529 ownership, implement actual DSP/GUI/preset storage in a small number of meaningful PRs, test and show exact SHA, Windows GUI screenshots, A/B audio, and honest unverified FAT gates.
