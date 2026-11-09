# ADR — ArSonMix Smart Zero-Friction Operator Workflow (Player + Mixer)
**Status:** APPROVED PRODUCT-DIRECTION DESIGN / NOT YET IMPLEMENTED · 2026-11-09
**Owner correction:** No user-facing Lab mode, admission gate, ARM/MONITOR chore, confirmation checkbox, routing-approval dialog, repeated Start, or technical wizard in the normal first-sound/player path.

**Authority:** This ADR clarifies and supersedes contradictory player-workflow sentences in [PRD](PRD_ARSON_PLAYER_LIVE_SETLIST_UX_20261109.md), [ARD](ARD_ARSON_PLAYER_NATIVE_AUDIO_ARCHITECTURE_20261109.md), and [agent workpack](workstreams/ARSON_PLAYER_IMPLEMENTATION_WORKPACK_20261109.md). Preserve [single approved /preview/](../AGENTS.md), correct physical output identity, actual PCM ACK and one JUCE render callback. It does **not** claim implementation, and does not authorize unverified hardware rerouting.

## A. Audit: what was wrong with original M0–M4 design

| Previous instruction (PR #481) | Why it burdens ordinary users | Binding replacement |
|---|---|---|
| First PLAY requires existing explicit route **or user confirms proposed MAIN route** | Extra dialog just to hear a selected music file | **Silent, atomic native auto-provision** of dedicated MUSIC ChannelId, typed PLAYER A source and WIRE to the **already verified / currently authorized MAIN** when there is no intentional user custom route |
| Current-song double click shows **warn/confirm** before Play Now | Unnecessary modal while performing | Direct `Play Now` action with native de-click/fade; default single-click **never** cuts current song |
| Separate PERFORMANCE, CONTINUOUS, ADVANCED “modes” | Exposes implementation thinking | One Player; native default `Stop after song`, small `Auto Next` toggle only if needed, contextual `More` options (no mode chooser) |
| Live Seek Lock settings/confirmation | Hidden setup to avoid accidental jump | Position is informational while playing; seeking while playing requires purposeful pointer drag/keyboard seek, short ramp, no lock checkbox |
| No auto-create routes on deck load/play | Forces mixer engineer workflow before music | **First-used PLAYER A automatically prepares dedicated source route to an already trusted MAIN**, without touching other channels or output pins |
| Player says “PLAYING” while MAIN unavailable | Confusing silent audio | Distinguish `Preparing / Ready / Playing / Playing — no MAIN route / Output unavailable` from actual native readback, no fake success |
| User may need extra APPLY/START/MONITOR ON after normal source attach | The product is asking users to operate engine internals | Source attach/Play authorizes one native prepare/publish/ramp/route transaction; no manual re-arm or monitor toggle where output is previously authorized |
| Generic ERROR / source graph fault strings | Requires user to diagnose DSP and drivers | Translate native fault to 1 clear contextual action; automatic bounded repair of *same verified* device/session; diagnostics under Details |
| Three tabs as required navigation | Too many decisions for first music | On first open, big `Add Music` and one list; `Library`/`Queue` appear as secondary links when useful; no “Create Playlist” prerequisite |
| Optional disabled pitch/speed controls posing as features | Creates false expectations | Do not show controls until functional DSP is proven; then show always-visible simple KEY and TEMPO with instant reset |
| Device startup still requires “consent” for routine unchanged session | Confuses user permission with internal safety | **Fresh Play click is the intentional action** for already-verified, identical output identity; identity-change never silently selects another port |

## B. Golden mental model — the user never sees a signal-graph admission process

**Familiar action = familiar result:** drop music files, click one song, click Play. The native app handles import/index, find verified render device, source preparation, reusable MUSIC channel, default route, PCM scheduling, DSP and ACK. A musician never needs to know terms ChannelId, ASIO, graph revision, armed, pin masks, MONITOR, or buffer size.

**Core deterministic workflow (happy path, existing verified output):**
```text
Open mixer
  → Drop one song OR click Player > Add Music (native file chooser only)
  → first imported song becomes selected/READY automatically
  → Click PLAY
  → software finds/creates native PLAYER A + dedicated MUSIC ChannelId (once)
  → software wires MUSIC→MAIN if this is a factory-owned, never-user-edited route
  → native render preflights output identity, PCM, sample rate and buffer
  → native ACK, ramp up music through existing channel FX/fader/MAIN to the SAME verified output
  → song ends: short fade and STOP; next title preloaded but NOT played
```
For 1 file, no explicit playlist creation; for multiple files/folder, automatically create an editable ordered `New Setlist` (prefer folder name if unambiguous). User may rename at leisure. Metadata extraction and waveform can finish lazily; PLAY waits only for minimum decoder buffer, not full-song scan. Import is non-destructive, no duplicate physical copies.

### Context flows and exact operator actions

| Situation | User input | System behavior | User-visible result |
|---|---|---|---|
| First import, one MP3, existing verified output | Drop song → Play | Decode/prebuffer; provision music channel if absent; route MUSIC→verified MAIN; invoke existing FX/fader/volume | Music audible; channel meter moves; no setup |
| First folder import | Drop folder → Play | Order tracks predictably (file order/natural sort), create named setlist and queue; preload first | First song plays; upcoming visible |
| Subsequent launch | Play | Restore last verified device and logical music route; verify actual identity, prepare, start only on this **fresh user gesture** | Same mixer, same music; no reconfiguration clicks |
| While singer is performing | Click another song once | Highlight/prepare as NEXT; do not alter CURRENT, MAIN, microphone or playback | NEXT changes, current uninterrupted |
| Switch song immediately | Double-click row or `Play Now` | Prepare new song, smoothly release current, start new at boundary (no approval modal) | New song plays promptly; clear title/readback |
| Jump to next prepared song | NEXT | When playing: fade to NEXT and play; when stopped: make NEXT ready; never spawn extra “Load” action | One direct result |
| Adjust key/tempo | Press +/−, reset | Worker prepares DSP; short smooth native transition and applied readback | KEY −2 / SPEED 95%, music changes, mic unchanged |
| Reopen player during playback | Player toolbar | Show existing CURRENT/queue, no graph edit | No meter/transport interruption |
| User deliberately unplugged MUSIC→MAIN | Press Play | Respect persisted `SOURCE_ONLY_USER`; play deck and meters, MAIN silent; display one inline `Connect to MAIN` action | Does NOT secretly undo user routing |
| User intentionally routed MUSIC to BUS | Press Play | Preserve BUS and sends; do not auto-add second MAIN dry send | Exact intended BUS chain |
| Existing MAIN is muted | Press Play | Preserve mute; indicate `MAIN muted` with obvious existing Mute control | No invisible auto-unmute |
| Same approved audio driver temporarily stalls | no input | Bounded off-RT retry using same identity/ports; keep mixer/doc intact; if loss persists pause, show small `Output unavailable` | No error dialog or surprise reroute |
| User selects different audio hardware | Select intended output once | Verify real port identity and output; atomic bind; honor current user PLAY intent only if still pending and safe | No ARM checkbox, no separate APPLY/PLAY cycle |
| Original file is missing | Play | Attempt safe same-media relink by identity from authorized library/known folder; otherwise keep row, skip auto queue safely, inline `Locate File` | No crash or blocking modal |
| One song unsupported or corrupt | Play | Reject only that song; retain playlist/mixer; continue other work | Inline `Can't play this file` |
| Restart/crash restoration | Open app | Restore mixer, setlist, selected song without music autoplay; prepare when safe | User presses Play deliberately |

**Semantic distinction:** `STOP_AFTER_SONG` is a sensible singer default, not a “Performance Mode”; `Auto Next` one compact option for ambience/podcast. `Play Now` is an intentional destructive command and does not need another consent screen. `Delete from setlist` should offer **Undo** instead of a blocking confirmation and never remove actual media from disk.

## C. Smart Native First-Sound Orchestrator

Propose a **single control-thread product workflow coordinator** (name e.g. `PlayerFirstSoundCoordinator`, may reuse existing owner in `src/Main.cpp`). Do NOT add second DSP engine, audio callback, native dispatcher or player-as-WebAudio. Its only job is mapping an ordinary user gesture to existing native graph transactions.

```text
USER Play(requestId, songId)
  -> ensure song exists + worker-produced PCM ring READY
  -> ensure same previously verified/present physical MAIN output binding
  -> resolve persisted player-owned channel/source binding (if valid)
  -> else provision a new dedicated ChannelId with typed PLAYER A stereo source
  -> if and ONLY if route is factory-owned and never operator-edited:
         provision MUSIC -> existing verified MAIN in same graph transaction
     else preserve accepted operator WIRE/BUS/SOURCE ONLY exactly
  -> compiler/preflight, snapshot publish, verify native graph and source readback
  -> publish sample-boundary transport PLAY, short gain ramp
  -> expose true Playing / route status and song cursor
  -> on failure: preserve previous mix, report one actionable inline reason
```

### Ownership and idempotency

- An internal `PlayerOwnedChannelBinding` record stores `deckId, channelId, createdByPlayer, routingPolicy, operatorEdited, graphGeneration, sourceBindingRevision`. Never hijack an existing strip merely named “Playback”, “Music” or “CH09”; preserve every existing input, FX, bus and fader. If old client cannot add dynamic channel, fix dynamic graph first; do not hijack CH1 or require Lab.
- Distinguish `FACTORY_DEFAULT_MAIN` from `USER_CUSTOM_ROUTE`, `USER_SOURCE_ONLY`, and `UNRESOLVED_HARDWARE`; the default route can be installed once with no prompts on a **trusted existing output**, but no automatic “repair” over intentional edits.
- Always issue an idempotent transaction keyed by `requestId` and current graph/device generation; repeated Play/Load, stale ACK, or UI retries cannot create multiple MUSIC channels or duplicate MAIN sends. Transaction rollback on stale revision/FX failure leaves earlier accepted graph intact. Bounded automatic internal retry only for *transient revision conflicts*, not invalid hardware/device substitutions.
- Player activation and source/WIRE link may be prepared proactively on file import (without audio or hardware changes). If graph/codec is not ready, PLAY remains pending in the same workflow, not a requirement to tap “Apply” or “Start”.
- If native sample/graph output is provably unavailable, never show “Playing with sound” falsely; block advancement of active song clock until a real output can be used, unless the user explicitly chose `SOURCE ONLY` or non-MAIN output. Producer readiness, transport, graph-connected and physical-output readiness are distinct facts.
- Existing mic input/monitor must follow same operator-first intent: selecting a verified hardware IN on a connected-to-MAIN live channel should prepare and deliver sound automatically, not require MONITOR OFF/ON/ARM/START. No hidden second engine switch; mic physical routing acceptance remains separately tested.

### Hardware safety without user-facing bureaucracy

1. **Trusted unchanged endpoint:** output identity, physical pin roles, actual device epoch, and saved user output policy all match real hardware. If mixer is already rendering, no new physical device action: attach source and play. If app restarted disarmed, the user’s **fresh explicit Play** is the authorization to activate only that same known output, after non-RT preflight and gain ramp. This is NOT spontaneous auto-ARM on application launch.
2. **Unknown or different output:** do not guess a device, switch from ASIO to WASAPI/Realtek/VB Cable, or silently unmute MAIN. Display `Select output` adjacent to Play. The user picks an available endpoint once; system validates and completes the original requested playback within the same transaction (only if request remains current). No acceptance checkbox, modal second APPLY, extra MONITOR toggle or double-Play.
3. **Intent supersession:** STOP, Pause, another selected song, device change cancellation, and navigation away cancel pending Play. If a device returns after a long outage, do not resume unexpectedly hours later. A short bounded retry is allowed only while continuous active Play intent and identical verified output identity remain; otherwise show Ready and wait for next explicit Play.
4. **Always honor MAIN MUTE/SOLO, explicit SOURCE ONLY, user-set faders, saved output policy and emergency MUTE.** Automatic default routing must never bypass them.
5. Preserve existing safety invariant “no automatic output to an unverified different device”. This ADR updates **routine operator workflow** and replaces older references requiring a separate user ARM after an explicit Play on an already verified, matching output; engine controls remain internal.

## D. Information architecture and responsive GUI — no Lab

**The only interface is canonical assembled `/preview/`**. Add small `Player` toggle and native miniplayer. Desktop panel placement and hardware aesthetic follow already-approved mixer, not a new screen. Compact fader/MAIN remain stable.

**Zero-content state:** show **Add Music** and brief one-line guidance (“Drag music here, or choose files”); no three tabs full of empty placeholders or instructional wizard.

**Has music:** auto-select current setlist; show title, progress, clearly sized play/stop/next, key ± and speed ± (when DSP real), and dense upcoming songs. `Library`, `Queue`, `New Setlist`, `More` are available as unobtrusive controls, not mandatory steps. Default mini bar remains usable with player panel collapsed; keyboard/focus interactions never steal mixer fader gestures.

**Tactile controls:** one consistent compact dark-metal/cyan style; shared button sizes and alignment; legible numeric key/tempo (not different knob treatments). Large enough to touch primary PLAY, not giant UI cards; 12–14 px standard body; 36–40px target for primary transport; responsive at 1366/1440/1680/1920 and 150% DPI.

**Transport semantics:** single click row = select READY if stopped, prepare NEXT if playing; double-click row = Play Now with safe native fade; NEXT while playing = play next; NEXT while stopped = prepare next. Hidden hover is never the sole way to perform a primary action. `Auto Next` toggle explicit if user wants sequential background music. Selecting list/setlist never interrupts active playback.

**Errors:** one-line inline message near command with a single useful action only when necessary: `Output unavailable — Choose output`, `File moved — Locate`, `Music is playing but not routed — Connect to MAIN`. Never show graph faults, `staleRevision`, `not admitted PCM graph`, Lab, JOIN/ARM/START or raw debug states to ordinary users. Detailed error code accessible from `More → Diagnostics` for engineers, no modal.

**No user-facing checkbox consent / confirmation dialog** for normal Play, Next, Source, FX, track transitions, source restore, routing to existing trusted output or error recovery. Destructive external hardware/format changes still require an explicit selection, not hidden action; deleting files from disk excluded from player UI.

## E. Automatic decision policy and hard boundaries

| Decision | Automatic when | Respect user's choice when | Error fallback |
|---|---|---|---|
| Create MUSIC channel | First valid player use; no owned channel present | Existing valid player-owned ChannelId preserved | Capacity error → one inline action to free/select route; never hijack occupied channel |
| Assign PLAYER A source | Owned channel newly created or its persisted source remains player-owned | User explicitly changed source of formerly player-owned strip | Create new owned strip if capacity; never overwrite edited |
| Connect MUSIC→MAIN | New owned channel and trusted existing main route; no edited path | User set BUS-only, parallel send, SOURCE ONLY, custom routing | Inline `Connect to MAIN`, never automatic bypass |
| Select sound device | Exact currently open/trusted output | User selected different hardware/driver or MAIN mute | Context `Choose output` once; no default alternate speaker |
| Auto next | **Never by default** for singer; enabled explicitly by one toggle | Set breaks and per-entry stop override | Stop cleanly |
| Begin audio | User explicitly presses Play and native source ready | User presses Stop/Pause or output unsupported | Remain READY and offer contextual output/media action |
| Retry | Short, bounded same-identity transient error | STOP/cancel or identity mismatch | Visible Ready/error; no infinite retry |
| Save session | On accepted change via bounded atomic background store | Playback itself not autostarted on restart | LKG restore and diagnostic details; no data loss |
| Pitch & tempo | Cached prepared processing on worker | Precise user steps; never auto change song key for singer | Preserve original audio if DSP unavailable; show accurate state |

## F. Acceptance as beginner journey (must be tested end-to-end, no fake statuses)

**B01:** Fresh verified-install launch, drag 1 song then Play: after initial file chooser/drop there is **one playback click**. No PATCH/ARM/MONITOR/Apply/Mute workaround; native PCM reaches existing verified MAIN and music meter moves. Fail if user is required to understand source/WIRE.

**B02:** Import 20 files: setlist generated/ordered automatically; one-click song selects, Play uses selected, Next predictable; no forced create/rename/save playlist before sound.

**B03:** First player sound creates one stable MUSIC channel and one correct default MAIN route. Repeated 100x Play/Stop/Load does NOT create duplicates or erase other channel FX, faders or custom buses; survives show save/restart.

**B04:** Mic on real ASIO IN1 and PLAYER A run simultaneously with vocal FX; no MONITOR ON toggle or callback changes; no latency regression or crackles caused by file reader starvation.

**B05:** Deliberate SOURCE ONLY and BUS routing stay untouched on any Play; meter remains, MAIN silent for SOURCE ONLY; one-click explicit `Connect to MAIN` restores when requested.

**B06:** First Play after app restart on exact same verified physical device wakes appropriate approved output without secondary ARM/START; no autoplay on launch. If identity differs, one `Choose output` selection only, not a cascade of dialogs.

**B07:** On current song, selecting other row/setlist never interrupts; double-click Play Now transitions without popup/approval, with measurable click-free fade and native ACK.

**B08:** Repeated route revision conflicts, device epoch change, missing song, broken codec and decoder stalls are internally retried only where valid; otherwise one actionable inline error. Current live mic/accepted graph unaffected.

**B09:** No accessible control or production status text contains `Lab`, `test patch`, `native admission`, `route approval`, `ARM`, `MONITOR OFF`, `Apply Graph`, `Reset PCM`, `permission checkbox` in normal player/first-sound flow. Engineer Diagnostics is read-only and never prerequisite.

**B10:** Approved `/preview/` parity, stable MAIN/faders, 1366/1440/1680/1920 responsive screenshots; keyboard, touch, hardware-like tactile controls and browser-native context menu suppressed in installed app.

**B11:** 44.1/48/96 kHz, 64/128/256 frames, 30-min ASIO plus secondary output; no audio callback disk, locks, allocations, decoding, FFT, JS audio or undue mic delay.

**B12:** If output disconnected, codec unsupported or player is source-only, UI never lies with unconditional “Music is audible”. Playback state, device readiness and route readiness are reported separately.

**B13:** No confirmed unknown physical output or surprising new destination. Emergency mute and real user-provided routing override all smart defaults.

## G. Agent implementation priority / verification

1. **Before source code:** compare latest repo and note all product-gate conflicts in old PRD/ARD/session ADR; treat this latest user-corrected ADR as **normative** for regular-user workflow. Trace native route and source publication, including dynamic channel.
2. **M0:** deterministic golden test of `ensurePlayerReadyToPlay` as pure planner first: new/owned edited/none/MAIN muted/SOURCE_ONLY/BUS/user-changed/unknown output/epoch transition; no ghost audio, no duplicate route. Reuse accepted graph transaction and device identity policy.
3. **M1:** implement *actual* Play end-to-end, not backend skeleton: drop song → native worker → MUSIC channel → accepted MAIN → PCM; native readback; 20-file setlist; no UI gate.
4. **M2:** proven pitch/time DSP quality and independent vocal monitoring; only display functioning controls; quick applied native ACK.
5. **M3/M4:** auto next, cues, advanced deck and crossfade with same easy default; P0 first-sound simplicity must persist after advanced functionality arrives.
6. **Test with the beginner script B01–B13** as a release blocker, not only a CTest/Chromium screenshot green check. Hardware FAT PENDING until actually run.

**Code ownership:** control side may own orchestrator; `DynamicPreparedMixerBlock` and `DynamicNativeSessionPublisher` still own PCM graph, and existing `LiveAudioEngine` owns only physical audio callback. Never embed separate mixer path in player or substitute a mocked /preview/ state.

## H. Research basis (UX patterns, not implementation claims)

- [Stage Traxx 4.1 Queue](https://stagetraxx.com/docs/v4/playback/queue/): queued/current separation, queue management and opt-in autoplay. Note that its automatic queue can replace the prior queue on a tap; **do NOT reproduce that destructive tap behavior** in a live mixer.
- [Stage Traxx 4.1 Playback](https://stagetraxx.com/docs/v4/playback/): accessible transport and optionally restricted seeking; use purposeful drag instead of requiring a user “Seek Lock” setting.
- [Stage Traxx 4.1 Playlists](https://stagetraxx.com/docs/v4/library/playlists/): stop-after-song and show set boundaries are natural for singers.
- [Mixxx library/Auto DJ](https://manual.mixxx.org/2.5/en/chapters/library): search and queue/recovery without losing metadata, but no default DJ deck clutter.
- [Approved ArSonMix mixer](https://masarray.github.io/arsonmix/preview/): one established chrome, preserve pinned MAIN and native bridge.

**Release fence:** this is an architecture/product policy, not running code. Documentation-only edit does not require expensive CI. Do not merge changes to any alpha.6 release branch.
