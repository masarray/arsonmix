# ArSonMix — CANONICAL NEXT-THREAD HANDOFF (2026-10-29)

> **ENTRYPOINT SUPERSEDED 2026-10-30:** New threads **must start** at [EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md](EXECUTION_HANDOFF_ARSONMIX_MIXER_REFORM_20261030.md) and its canonical [ARD](ARD_PRD_MIXER_IO_AUX_BUS_REFORM_20261030.md)/[PRD](PRD_ARSONMIX_PRO_MIXER_WORKSPACE_ROUTING_20261030.md), NOT this old #129/old screenshot implementation sequence. Native MUTE, live FX identity and hardware FAT rules below still apply.


> **STATUS SUPERSEDED BY LATER RECONCILIATION (2026-10-29 17:55 WIB):** See [canonical status after R1 and R0 premium routing merged](CANONICAL_PROGRESS_RECONCILIATION_20261029.md). The original handoff below remains historical evidence. Do not run its old next steps without checking current main / PR statuses; R0B is #149 and #147 is the open ACK fix.

> CURRENT CONTINUATION ENTRYPOINT. Read this before editing. This supersedes status claims in the dated 2026-10-25/26/27/28 handoffs, NOT their historical engineering evidence. Every new thread must re-read live main, open PRs and CI because this snapshot can age.

## 0. Verified repository state at handoff

- Repository: https://github.com/masarray/arsonmix
- Verified main HEAD before this documentation-only handoff: `abcdf2b9e8ee9d58a4da116c84c5bec382346dd2`. PR #129 (https://github.com/masarray/arsonmix/pull/129) is **MERGED**; merge contains stable first-open JUCE Audio Setup layout, neutral gray OFF / green ON toolbar, subtle solid-state BTN-180 hover, and basic saved scene UX.
- Exact-head main CI (all **SUCCESS**): Windows Native VST3 Host https://github.com/masarray/arsonmix/actions/runs/40765946915 ; Chromium UI smoke https://github.com/masarray/arsonmix/actions/runs/40765947039 ; GitHub Pages https://github.com/masarray/arsonmix/actions/runs/40766102513 .
- Windows artifact (run above): ID `12849759138`, `ArSonMix-GUI10CH-P012-Verified-Hardware-FAT-Windows-x64`, 4,180,815 bytes. The artifact's *name* says Verified Hardware FAT but green CI **is not proof** of physical audio/device FAT. Verify SHA / archive manifest when downloading.
- Main PR lineage: #122 atomic FX reorder/drag reconciled; #124 11 equal-width CH/MASTER + LCD meter, CSS/landing and BTN-180 previous change; #129 corrected bad toolbar colors and JUCE dialog stretch.
- Active competing branch: PR #128 https://github.com/masarray/arsonmix/pull/128 is **OPEN**, with earlier 2026-10-28 proposal and passed CI for its own head (not current main). It overlaps Main.cpp, WebView GUI, Button 180, and snapshots. DO NOT merge it wholesale onto main or silently reintroduce 2026-10-28 behavior. Its useful **Shows/Snapshots advanced UX** and `docs/ARD_SNAPSHOT_SHOWS_NATIVE_TRANSACTIONAL_20261028.md` are still worth reading and selectively porting with current source ownership. The ARD is currently in PR #128's branch, not necessarily main.
- Other longstanding open PRs #115/#117 (overlapping 10-channel routing), #113 (historical handoff), #60 (experimental meter), #59 etc. are not merge instructions. Inspect current diff against main before touching.

## 1. User's hardware observations and non-negotiable design requirements

The owner confirmed that GUI now runs smoothly on low-end/onboard GPU Windows hardware. DO NOT regress this into expensive hover/blur, giant elements or multiple animation loops. Native screenshot at 1593x928 shows 10 CH + MASTER and left rail. Next screenshots from the native Audio Setup show initial window content stretched to irregular sizes; moving the window forces repaint and correct layout. Device list changes dynamically when input/output chosen (including VB-Audio Virtual Cable and Realtek); correctness requires independent real PC verification, not merely a JUCE compile.

Latest explicit visual contract (CORRECTS #124, #128 old palette description): toolbar-action buttons are normal light-steel/GRAY when inactive; **green only when .selected/.active/aria-pressed true**. BTN-180 generic controls retain semantic colors for MUTE/SOLO/STEREO/HPF; no all-buttons-green look. Ultra-light hover permitted only as static bright edge / simple tint; no CSS filter:brightness(), blur(), large glowing box-shadows, transform/scale transition, or timer-driven hover. Press must feel immediate. One master and each of the ten strips have the same width, fader/ornament geometry. Meter remains dark inset thin LCD glass with fixed red/amber/green LED zones, native smooth ballistic 30-ish FPS (not expanding colors).

## 2. Implemented / protected baseline — DO NOT ERASE

| Area | Main status / important qualification |
| --- | --- |
| FX Rack drag ghost + drop / atomic logical move | Merged #122; stable physical processor identity and logical order; avoid detach/re-add during MOVE. |
| FX ON/OFF de-click | Merged #107; per-physical-slot FxBypassEnvelope; do not revert to bare bypass atomic flag. Audible hardware FAT still required. |
| DearVR PRO Orbit | AUTO Orbit, POLAR vendor mapping, dot travels on path, state survives adding/moving other FX (integrated #79/#94/#98/#103/#105 and later fixes). Preserve instance association and editor lifecycle. |
| Native meter + GUI responsiveness | Native MASTER peaks, fixed-zone LED, eased fall, one shared scheduled renderer at ~30 FPS; CSS no blur; low-end GPU confirmed lighter. |
| Fader + layout | Approved smooth folded-metal recess #120, uniform 10CH+MASTER min-width 128px #124, LCD meter housing, source-owned CSS. |
| Toolbar and audio device setup | #129 merged: gray idle / green selected; BTN-180 hover only bright edge; AudioDeviceSelectorComponent setContentOwned(selector,false) with stable 740x590 window / post-show resized+repaint. **Physical first-open DPI test pending**. |
| Snapshot sidebar basics | #129: named save/confirm overwrite, stored session localStorage, recall, delete and persistence error message. Still UI-only and incomplete: NOT native full 10CH VST3/routing recall. |
| Native VST3 sounds | `presetSave/presetLoad` in src/Main.cpp and `NativePresetArchive` store real CH1 plugin binary states into .armix archives; require MUTE OUTPUT and maintain graph transition safety. Separate from browser localStorage snapshots. |
| Portable Studio | #116 verified backup with Plugin Vault, native preset/manifest, source-relative storage. Do not claim localStorage browser scenes are included; native P4/P5 workspace scenes/restore not done. |
| Native 10CH routing | P1E–P1I merged: opt-in device-bound Routing LAB with real physical-mask routing, one stereo-compatible VST3 per strip, live fader/PAN/MUTE/SOLO and true per-strip peaks under active LAB. NOT full DAW; master FX, full rack multi-FX, integrated AUX semantics, PDC and hot scene recall not complete. See `docs/P1I_UNIFIED_10CH_REALTIME_CONSOLE_20261028.md`. |
| Hardware crackle #15 | OPEN; zero-VST WASAPI input/output/capture crackles not proven solved. Distinguish capture discontinuities from measured render underruns. |
| Browser / landing | `https://masarray.github.io/arsonmix/` and `/preview/mixer.html` publish current source GUI from main. Browser is a UI preview; not an audio-FAT test. |

## 3. Very next task — physical validation of PR #129 and minor fixes

1. Fetch exact new main HEAD and verify PR #129 changes in `src/Main.cpp` `AudioSetupWindow`/`showDeviceSetup`, `web/styles-src/20-button-180.css`, `web/app.js`, `tests/ui_smoke.py`. Current main baseline above is not a permanent pin.
2. On real Win11 with 100/125/150/175% system scale and different app launch/reopen cycles, click AUDIO SETUP repeatedly. Verify first paint is normal (no manual drag), after selecting Windows Audio/ASIO, changing device, enabling channels, sample rate and buffer, resizing, minimizing/restoring and moving between DPI monitors. Device selector's owned child component must stay within visible client bounds; no clipped list, excessive font scale, white blank, delayed geometry. If JUCE default sizing still wrong, instrument `resized`/`visibilityChanged` and device-list layout to isolate DPI and OS-peer event order; fix in message thread, not timer/poll hacks. Preserve in-progress audio device approval / DIRECT restore fences.
3. Verify toolbar OFF gray, selected green and semantic toggle behavior by **computed styles in actual native WebView2** and browser. Hover test with a 10+ sec pointer sweep on onboard GPU. Measure p95 pointer input-to-paint latency and rAF/DOM commits before claiming no regression. The CSS implementation is edge border-color mix only; if causing sensitivity, disable hover instead of adding layers.
4. Add realistic snapshot manager tests (save, restart, recall, overwrite confirmation, delete confirmation, malformed data, duplicate/unsafe names, localStorage quota/unavailable) and test that selecting a snapshot *does not lie* about native VST/audio. Note possible scene-list rollback consistency on persist failure and no genuine native recall of CH2-10.
5. Preserve `web/styles-src/*` as source of truth and synchronize `web/styles.css` and `AR-MIX-Live-FX-Host-Standalone-v0.3.html` using `python scripts/build_ui_css.py` / `--check` and existing UI bundle tests. Only one Windows CI and Chromium CI per meaningful change; no overlapping full builds.

## 4. Next milestone — Soundcraft Ui-class Shows / Snapshots, NOT naive localStorage-only

Product behavior: compact left-rail **Show selector** (a show can contain many snapshots); `SAVE NEW`, `UPDATE`, `RECALL`, `RENAME`, `DELETE`, optional duplicate/notes/last-saved; active name and dirty indicator; searchable or compact list; immutable factory defaults; deliberate confirmation for overwrite/delete. Snapshot should recall the *complete native mixer* from a prior session, with persistent data that travels in Portable Studio Backup. User-friendly small widgets, no heavy cards/modals.

Engineering SSOT:
- Draft `ShowV2` / `SnapshotV2` native schemas with stable UUID, schema version, timestamp, compatibility/capabilities, 10CH source routing and device-port IDs, strip gain/PAN/MUTE/SOLO/HPF, pre/post sends, AUX/bus/master, FX order, bypass, vendor VST3 binary state/preset identity, Orbit parameters and automation state, snapshot safes/recall scope. Include future extensibility and migrations. *Never* blindly restore physical device identifiers on a different PC; explicit remap and fail silent.
- Use a single native workspace persistence authority, not independent conflicting browser-localStorage and native sound lists. UI client is a projection of native scene metadata. Existing localStorage scenes can be imported with explicit **UI-only legacy** labeling; do not claim to migrate vendor states that were never stored.
- Save: prepare/stage off callback, limit file/manifest sizes and state blobs, validate plugin identity and bounded I/O, write temporary archive + fsync/flush + atomic replace/rollback, verify content/hash. Handle missing vendor plugin and external sample libraries without destroying current live state.
- Recall: preflight every plugin/routing/driver and snapshot-safe mask; resolve capabilities, prepare new immutable native graph/control set OFF RT, obtain exact device epoch/revision; mute for any non-live-safe structural or vendor state operation. Publish at one callback boundary with ACK; crossfade/ramp only where DSP alignment is safe; on missing plugin, schema error, stale device or timeout reject with old graph intact (or fail silent), never claim success prematurely.
- GUI: selected/pending/active states distinct; show preview/diff before recall, confirmation when critical routes change, Undo last recall with retained previous safe state, Channel Safes for protected live strips, optional selective recall; record last good performance as recoverable autosave journal (not every fader motion to disk).
- Backup: include new native shows/snapshots in `Studio/` and current verified backup manifest; add tested non-destructive restore and conflict resolution (Portable Studio P4/P5). One app for all users, same feature set; private vendor VST never bundled with public EXE.
- Acceptance: deterministic fake IO/VST and malformed/partial archive tests, JUCE MSVC+CTest, UI smoke and native real-hardware FAT with concurrent audio; real 10CH and Orbit vendor state, preserves tails where feasible, no clicks/hangs on repeated save/recall. Physical ASIO/WASAPI/driver mismatch must not open unintended speaker. Existing #15 remains independent.

## 5. Next-thread execution order (mandatory)

A. Check latest main/PRs/CI, read this handoff, `AGENTS.md`, `docs/GUI_BASELINE_LOCK_2026-10-25.md`, `docs/ARD_SMART_AUDIO_ENGINE_V1_2026-10-25.md`, `docs/P1I_UNIFIED_10CH_REALTIME_CONSOLE_20261028.md`, `docs/PORTABLE_STUDIO_P3_NATIVE_VERIFIED_BACKUP_20261028.md` and PR #128's snapshot ARD. Do not trust older status in `docs/ARSONMIX_CROSS_THREAD_HANDOFF_20261028.md`.
B. Validate newest #129 native dialog on real Windows; fix only evidence-backed issues first. Add targeted native regression tests for dialog geometry/DPI where feasible.
C. Diff PR #128 against current main; salvage only UI/ARD improvements not already delivered by #129. Avoid replacing latest CSS, meter, FX drag/bypass and 10CH Routing LAB with stale branch files. If #128 is obsolete, document then close as superseded after verifying unique changes.
D. Implement native ShowV2/SnapshotV2 in staged, small reviewable PRs: data model+round-trip tests → validated archive+workspace/backup → read-only GUI list+metadata → safe native save/recall transaction → safes/undo/migration/FAT. No broad trial-and-error rewrite.
E. Before ANY merge: compare exact head to live main, verify Windows Native x64+CTest, Chromium+Firefox interaction, CSS source and embedded offline parity, output artifact provenance and screenshot. CI PASS ≠ no-audio-crackle.
F. Provide final real artifact/link and accurate feature limitations, not an unverified promise to deliver later.

## 6. Anti-regression checklist

- CH1 native FX Rack drag/drop atomic move; no callback stop/restart on MOVE; bypass anti-click envelope stays on physical slot.
- dearVR POLAR and Orbit dot/trajectory keep exact plugin instance and survive ADD/REORDER; no reset.
- Meter LED 7px fixed threshold colors/peak clip, MASTER stereo, bounded smooth ballistic / 30FPS light GUI.
- Onboard GPU stable pointer movement, no blur/glow filters or per-hover full mixer repaint.
- 11 identical strip/fader geometry, folded metal recess, LCD meter, 128px safety/scroll.
- Toolbar OFF gray, active GREEN, immediate button press, optional single-border hover.
- JUCE Audio Setup first paint/layout correct; ASIO/WASAPI, endpoint/device recovery, safe DIRECT route.
- Real native Routing LAB token, epoch, device masks and strip DSP never replaced by browser dummy values.
- All VST3 Vault, native .armix sounds, portable verified backup and no private VST distribution regression.
- #15 zero-VST physical issue and native hardware scene FAT explicitly NOT marked resolved without evidence.

## 7. Quick continuation instruction for a new ChatGPT thread

Open `https://github.com/masarray/arsonmix/blob/main/docs/HANDOFF_ARSONMIX_NEXT_THREAD_20261029.md` and read fully. Then fetch current main and PR list; verify #129 is merged and exact-head CI status. Continue from `3` and `4`; preserve `6`. Implement small optimized changes and verify before merge. Never re-ask previously answered design intentions. Do not copy stale branch FX/GUI. When finishing, update this file with actual code/PR/CI/hardware evidence, current SHA and next steps.
