# ARD — ArSon Player Native Audio, Mixer Integration & Reliability
**Status:** PROPOSED ARCHITECTURE · **Date:** 2026-11-09 · **Baseline main:** `975ce7c0da6c5b54e95a5d263e29a42baf1a8289`

> Implements [ArSon Player PRD](PRD_ARSON_PLAYER_LIVE_SETLIST_UX_20261109.md) via the existing JUCE callback and approved `/preview/` GUI. **No code in this design PR.** The implementation must first reconcile all touched paths against fresh main and existing active PRs. The exact suggested types/filenames here are *new proposals*, not existing symbols.

## 1. Codebase evidence and boundaries (audited on main)

| Existing file/contract | Observed status | Required change |
|---|---|---|
| `src/Main.cpp`, `src/LiveAudioEngine.h` | Native host has JUCE audio callback, ASIO/WASAPI and FX/secondary output | Own player lifecycle on CONTROL/worker threads; consume player PCM in **one existing callback**, never start another device |
| `engine/core/DynamicChannelRoster.h` | Stable `ChannelId`, separate `ChannelRole` and `ProcessingFormat`; a role is currently input/return/group/monitor/main | Keep processing ChannelRole unchanged; model PLAYER as a new *source kind*, not a synthetic hardware input nor an FX slot |
| `engine/core/DynamicPreparedMixerBlock.h` | `DynamicSourcePatch` currently `ChannelId, inputL, inputR, stereo` (physical callback ordinals) | Evolve to typed `SourceBinding` for Physical vs Player; preserve legacy physical source serialization and compilation; inject virtual PCM at scheduled input lane |
| `engine/core/DynamicNativeSessionPublisher.h` | Immutable accepted graph, live `deviceEpoch` + `graphRevision`, caller-supplied FX, sole render method | Add prepared player source leases to same accepted graph; rev/epoch fences, one per-callback PCM view; control-only publication and retirement |
| `src/NativeMixerSessionDocument.h`, `src/NativeMixerSessionStore.h` | ChannelId/mixer doc persists independently of hardware binding | New versioned PlayerLibrary and SetlistStore; mixer doc stores only source references/binding and deck preferences; **never embed song bytes or reader pointers** |
| `web/approved-native-bridge.js`, `integration/ApprovedNativeControlIntent.h`, `src/ApprovedNativeV8Projection.h` | GUI bridge/ACK and readback for native mixer | New versioned player transport command family; native player controls may change faster than topology, but source-binding changes always validate graph revision |
| `site/preview-v2/index.html`, `scripts/build_pages.py`, `scripts/build_approved_native_gui.py` | `/preview/` is assembled one authoritative public/native GUI; preview-v2 is internal source not a second product | Add component/style assets to **same** public Pages assembly and native bundle; native mock must visibly disclose no audio; do not ship separate `web/index.html` player |
| Active PRs `#468`, `#467`, `#469/#474/#476/#479` | Release candidate, Harmony and Vocal Throw work are independent | Never pull their draft DSP or release tag into player design/implementation; future BPM integration is optional |

**Important:** Existing `DynamicPreparedMixerBlock` comment indicates historical DYN1A staging, while `DynamicNativeSessionPublisher` comment indicates migration intent. The implementation agent MUST trace actual `LiveAudioEngine` callback dispatch, installed route acceptance and production path on CURRENT main; **do not assume an unused type is already hooked live.** Capture golden before/after native PCM fixtures for music and mic.

## 2. Non-negotiable invariants

1. **One audible PCM authority:** existing JUCE AudioIODeviceCallback / accepted ChannelId routing graph remains sole host render; no AudioAppComponent, WebAudio, second callback, hidden ASIO loopback or independent WASAPI player.
2. **No realtime blocking:** audio callback performs only bounded copies, preallocated interpolation/ramping and mix; no disk access, decoder, time stretch FFT, file open, allocation, lock, filesystem checks, logging, WebView messages or destructors.
3. **Separate clock domains:** decode/stretch produces **device-rate PCM** on bounded worker ahead of physical callback; resample for 44.1/48/96 kHz as needed. When music stalls, output silence (short ramp) without stalling mic/ASIO, record underrun off callback.
4. **Stable source identity:** `SourceId` (deck `A` or future `B`) is distinct from `ChannelId` and hardware pin ordinals. A channel chooses exactly one `primarySource`; SOURCE ONLY means source exists but has no outgoing WIRE/OUT. Multiple channel consumers tap same per-block stream (no double FIFO drain).
5. **Durable music vs ephemeral audio:** setlist, song-level metadata and routing references live in versioned JSON, not current audio device handle or graph scratch. Device change does not delete playlist or channel.
6. **Explicit trust:** A GUI press yields acknowledgement only after native validation and accepted transport generation; no simulated PLAYING/readback nor optimistic FX route.
7. **Safety and rollback:** output remap, new physical endpoints or unverified device require existing hard mute/arm policy. New player generation or missing file cannot silently mutate MAIN routing. Preserve previous accepted graph on rejected source edit.
8. **No regression:** if player feature is inactive, existing console behavior, ASIO monitoring latency, resource footprint and Show/Scene state remain unchanged.

## 3. Proposed ownership model and audio flow

```text
APPROVED /preview/ GUI            NATIVE CONTROL                        WORKERS
  Player drawer/mini bar ---intent--> PlayerCommandDispatcher ---load----> MediaCatalog/Decoder
    Library/Setlist store <---ACK----> PlayerSessionController  <---index--> Waveform/Metadata
                           |          (command admission)          Stretch/Resample
                           |                      |                      |
                           |             Immutable PreparedDeck (A) <-- bounded SPSC PCM
                           |                      |                      |
Mixer IN: [PLAYER A] ------graph edit----> DynamicSessionPublisher       |
                           |                      |                      |
                           +--> ChannelId-source binding----> [pre-FX source tap]
                                                       |                 |
ONE JUCE ASIO/WASAPI callback → Player PCM slice → native ChannelId mix/sends/FX
                                                       ↓
                                              BUS → MAIN → hardware / OBS
```

Suggested new packages (pure types can remain under `engine/player/`; JUCE dependencies only in `src/` and boundary):
- `engine/player/PlayerContracts.h`: typed IDs, command enums, errors, transport snapshot (no JUCE).
- `engine/player/PlayerTransportState.h`: deterministic state machine + transition acceptance (no JS).
- `engine/player/PreparedPlayerSource.h`: preallocated callback adapter, generation-aware SPSC PCM + non-blocking underrun/retirement.
- `engine/player/SourceBindingPlan.h`: strongly typed binding validation and per-block frame cache/shared source fan-out.
- `src/NativePlayerController.h`: JUCE lifecycle, native serialized intent, file loaders, non-RT threads, DSP readiness.
- `src/PlayerMediaStore.h`: song identity, versioned list/queue/playlist persistence, crash-safe writes and locate/relink.
- `src/PlayerDecodeWorker.h`: AudioFormatReader + sample-rate conversion + ahead-of-time DSP.
- `src/PlayerPitchTempoProcessor.h`: a testable adapter to pitch/time engine with disclosed latency, preallocated worker buffers and optional bypass at 0 st/100%.
- `integration/ApprovedNativePlayerContract.h`: request validation/snapshot projection, or extend existing native control/readback contract without alternate dispatcher.
- `site/preview-v2/assets/arson-player.{js,css}`: UI in the canonical Pages assembly; all audio JS forbidden.

Names are tentative until a design-fit code audit; avoid parallel schemas and duplicate control dispatchers.

## 4. Typed source contract and topology migration

**Legacy:** `DynamicSourcePatch{id,inputL,inputR,stereo}` assumes physical input indices. Do **not** represent Player A as negative pin -2, fake mic or fixed CH10. Introduce a typed tagged source:

```cpp
enum class SourceKind : uint8_t { none, physical, playerDeck };
struct SourceBinding {
  ChannelId channelId;
  SourceKind kind;
  int physicalL = -1, physicalR = -1; // kind==physical only
  uint64_t deckId = 0;              // kind==playerDeck only; A=stable ID
  ProcessingFormat format = ProcessingFormat::stereo;
};
```

Rules:
- `none`: zero signal, channel/FX/WIRE persistence retained. `physical`: legacy mask/ordinal validity unchanged. `playerDeck`: deck exists, prepared and native generation valid; stereo => L/R, mono conversion follows mixer format rules. Unknown values reject rather than coerce.
- Keep `ChannelRole::input` and `ProcessingFormat` model; player is independent *source binding*. A future `fxReturn` routing doesn't become a song source automatically.
- New schema accepts old docs with no player object; migrate physical source records losslessly; require backwards-compat tests. Avoid a breaking aggregate parser; add optional typed field/version rather than mutating all legacy numeric fields at once.
- Mixer editor `IN` picker has 3 groups: `HARDWARE`, `PLAYERS`, `NONE`; selecting a player applies native source-binding transaction only after accepted graph validation. Do not auto-create a route at playback-time or on deck load.
- Audio callback obtains `PreparedPlayerFrameSource` *before* graph scheduling, once per rendered buffer; all ChannelIds bound to same deck use same PCM slice at the same transport frame. Repeated sends are normal graph fan-out; reading/advancing a FIFO twice is forbidden.
- Deck playback uses graph render rate and block size. A device switch increments the existing device epoch; suspend/fence old stream, prepare new resampler/buffer, and resume **only if existing accepted-output safety permits**. Do not invent automatic re-arm.
- Changing playback file is a transport/deck-generation edit, *not* a ChannelId graph recompile. Changing source kind/connection is a graph edit requiring accepted revisions and safe snapshot publication.

## 5. Player transport finite-state machine

States: `EMPTY` → `LOADING` → `READY` → `PLAYING` ↔ `PAUSED` → `STOPPED/READY`; `ENDED` auto transitions to `READY-NEXT` (silent) under default mode. `ERROR` is **deck-local**, not mixer failure. A track may be selected as NEXT while current remains PLAYING.

| Intent | Precondition | Action and authoritative ACK |
|---|---|---|
| `ADD_FILES` | File picker result accepted | Worker imports valid files; per-file results, bounded batch; no transport interruption |
| `LOAD_TRACK` | Valid songId, file preflight | Async prepare new deck generation; if current PLAYING mark as NEXT only unless explicit `REPLACE_CURRENT` confirmation |
| `PLAY` | READY/PAUSED + output eligibility | Publish safe play command at callback boundary; ACK resulting `PLAYING` with sample cursor and generation; if output unavailable distinguish `READY_NO_OUTPUT` |
| `PAUSE` | PLAYING | Ramp to zero then halt sample cursor; retain buffered/current item |
| `STOP` | PLAYING/PAUSED | ≤20ms output ramp; reset to trim-start or user-configured cue; retain loaded song |
| `SEEK` | STOPPED or Live Seek Lock off | Build new decoder/DSP cursor on worker, generation swap at safe boundary; never raw `setPosition` from callback |
| `NEXT` / `PLAY_NOW` | Candidate prepared | NEXT default loads and stays ready; Play Now explicitly changes active song with fade; reject if candidate not ready without killing current |
| `SET_PITCH` / `SET_SPEED` | Supported DSP range & current generation | Worker stages changed DSP path; apply 5–20ms de-click (or crossfade generations); report requested vs applied; preserve elapsed musical time consistency |
| `TOGGLE_AUTO_NEXT` | User opt-in | Authoritative mode changes, persisted by setlist entry or app preference, visible at all times |
| `SOURCE_BIND` | Channel exists, graph valid | Native typed binding → accepted graph revision and deviceEpoch; rollback on reject |
| `RELINK` | Missing song and file picker result | Identity/fingerprint check + update media reference; transport unaffected unless song presently missing |

**Concurrent intents:** one serialized native writer per deck; monotonic `requestId`, `deckGeneration`, `libraryRevision`, `queueRevision`; drop/reject stale operations rather than reorder with timers. `PLAY` must not claim success while deck is LOADING or source not bound to any audible path; show `playing / not routed` separately if engine legitimately runs and current output is SOURCE ONLY.

**End behavior:** `STOP_AFTER_SONG` (default); `AUTO_NEXT` opt-in; `REPEAT_ONE` opt-in. `BREAK` boundary always stops unless specifically changed. Never treat selected row as a destructive play intent. Do not loop the last song to first silently.

## 6. PCM decode, ring and DSP quality pipeline

1. CONTROL accepts file path/source identity (canonicalize and restrict to user-selected accessible files), obtains metadata/decoder support; producer thread opens `juce::AudioFormatReader`, decodes e.g. blocks of 4–16K frames off realtime. AudioFormatManager supported list must be verified per platform/build; WAV/AIFF and conditional FLAC/OGG/MP3 compile options are **not** assumed guaranteed.
2. Convert source sample rate to current render rate and output two channels; choose downmix/channel mapping explicitly for >2-channel source (reject or policy). Peak guard/headroom and per-song trim gain before ChannelId FX; no implicit hard limiter/dynamic normalization in P0.
3. Time/pitch processing **off realtime**: candidate [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) MIT (verify pinned version, notices, Windows/Linux/macOS builds, CPU + listening A/B); stage alternatives and run audio golden quality. Pitch/key ±6, speed 80–120% initially, optional 50–150% after soak. DSP must report processing latency and introduce no delay into *live vocal paths*. Zero/100 fast path may bypass while maintaining smooth switch.
4. Worker writes bounded interleaved/deinterleaved stereo PCM ring tagged `{deckGeneration, deviceEpoch, startOutputFrame, length}`. High watermark = proposed 1–3s; low watermark triggers refill; max memory bounded as function of rate/channels/decks and budget. Size is configurable/tested, not magic unbounded 10-minute preload. Per-device 48kHz stereo float at 3s ≈1.15MB per deck ring (excludes metadata/overlap); verify memory at 96kHz/dual deck.
5. AUDIO consumes one fixed block non-blockingly, ramping gain. On empty/stale generation: emit silence and atomic underrun count, preserve mic processing; **never wait for file reader**. Do not request a reload or log from callback; control polls counters at safe cadence.
6. At stop/pause/seek/key/speed changes, stage a fresh immutable deck/ring with generation and schedule bounded crossfade/declick at block boundary. A generation race must never combine old cursor with new PCM; no shared mutable decoder from two threads. EOF is based on decoder+stretch **drained** state and actual output frames, not merely file-read EOF.
7. Future dual deck and crossfade: A and B have independent prepared rings; single callback gets one PCM slice each, envelopes gain A/B; **separate** source IDs if advanced routing requires distinct FX, otherwise combine pre-fader into designated music lane by user selection. P0 single deck must not pretend overlap is possible.

**Latency/sync:** Player DSP has prep/lookahead latency but music is prepared ahead of playback; expose actual cursor and audible clock. Treat cue markers/trim in source-time, and convert to rendered frames using ratio + compensation; avoid drift with 44.1→48k resample and repeated speed edits. Any per-source pipeline latency must be declared to graph PDC if it participates in live mixing with delayed music sources. No extra latency may be inserted on microphone's ASIO path simply to align music if not opted in.

## 7. Native command/readback protocol (proposed v1)

Use existing WebView→native event transport, **not** new JS AudioContext. Field names below are contract targets; add versioned adapters only once:
```json
{
  "schema": "arsonmix.player.intent.v1",
  "requestId": "opaque-unique-id",
  "operation": "PLAY|PAUSE|STOP|LOAD_TRACK|QUEUE_NEXT|SET_PITCH|SET_SPEED|SET_SOURCE",
  "deckId": "A",
  "expectedDeckGeneration": "17",
  "expectedLibraryRevision": "9",
  "deviceEpoch": "42",
  "graphRevision": "109",
  "payload": { "songId": "uuid", "semitones": -2, "speed": 1.0 }
}
```
```json
{
  "schema": "arsonmix.player.snapshot.v1",
  "authority": "NATIVE",
  "ackRequestId": "opaque-unique-id",
  "ack": "APPLIED|PREPARED|REJECTED",
  "fault": null,
  "deckId": "A",
  "deckGeneration": "18",
  "state": "PLAYING",
  "songId": "uuid",
  "playheadFrames": "124912",
  "durationFrames": "9950932",
  "sampleRate": 48000,
  "pitchSemitones": -2,
  "speedRatio": 1.0,
  "nextSongId": "uuid",
  "endMode": "STOP_AFTER_SONG",
  "sourceBoundChannelId": "51",
  "routeAudible": true,
  "underruns": 0
}
```

Revision fields: do not require graphRevision for pure queue edits if graph unchanged; require deviceEpoch/graphRevision for `SET_SOURCE`, and require exact deck generation for transport/dsp edits. Readback must distinguish `sourceReady`, `transportPlaying`, `routeAudible`, `physicalOutputReady` rather than one ambiguous “Connected/Playing” boolean. `PLAYING` can be true with no MAIN route; GUI must state `SOURCE ONLY — no main output`. Progress updates throttled to 5–10 Hz, event-driven transitions immediate; never send one JS event per audio block.

Fault taxonomy: `UNSUPPORTED_FORMAT`, `FILE_MISSING`, `FILE_CHANGED`, `DECODER_FAILURE`, `DSP_UNAVAILABLE`, `BUFFER_STARVED`, `STALE_GENERATION`, `STALE_DEVICE`, `STALE_GRAPH`, `NOT_PREPARED`, `NOT_ROUTED`, `OUTPUT_NOT_READY`, `INVALID_SEEK`, `PERMISSION_DENIED`, `BUDGET_EXCEEDED`. Reject malformed length, NaN, out-of-range, invalid IDs, duplicate requestIds, and non-finite speed/pitch on CONTROL; show actionable UI text without huge dialogs.

## 8. Durable persistence and asset identity

New files under `PortableWorkspace` logical data root (subject to existing store API):
```text
Player/Library.v1.json     # song catalog: uuid, title/artist, canonical/relative URI, size, mtime, optional fingerprint, key/bpm
Player/Setlists/*.json     # ordered entryIds + songIds + entry overrides (pitch, speed, gain, trim, endMode)
Player/Queue.v1.json       # optional crash recovery; ephemeral, opt-in to restore queue
Player/AnalysisCache/*    # regenerable waveforms/peaks and BPM/key provenance; never authoritative
```

Document invariants: stable song UUID, stable per-setlist entry ID; reference-count independent, no deleting source on playlist removal; relative path when within portable show folder, canonical absolute path + file locator fallback elsewhere; optional user-approved collect media, never background copy without space estimation; schema+checksum + atomic stage/HEAD/LKG strategy consistent with existing `NativeShowStore`. Per-song defaults and per-entry overrides merge in deterministic priority: **runtime unsaved override (current show) > setlist entry override > song default > factory default**; editing dialog explicitly offers “Save for this Setlist” vs “Save as Song Default”. On restart restore list/selection, NOT auto-start playback or re-arm hardware. Never put song bytes/huge waveform in Show JSON, and do not store privileged raw file paths in public Pages-only demo.

## 9. Testing, performance, security and licensing

**Unit fixtures:** transport transitions / stale generation; queue edit during active playback; end/auto/loop/break; migration of old hardware `DynamicSourcePatch` with bit-exact PCM; 2 ChannelIds bound to same deck consume one advancing source; WIRE/source-only bus & MAIN; invalid ring epoch and starvation; pitch FFT fundamental ratio `2^(n/12)` and duration rate with tolerance; 44.1/48/96k sample-rate conversion; duplicate/out-of-order ACK; corrupt files and file moved; save/reload identity and crashes.

**Native integration:** one 10-min mock backing track + concurrent zero-FX/FX mic test; proven actual callback, 64/128/256 frames, 30min physical ASIO soak, no xrun/stall/regress in monitoring; Windows MSVC+CTest, Linux/macOS compile/package; headphone PFL isolation, OBS secondary, device disconnect/reconnect, EQ/Compressor/Throw unaffected when present; file missing while running; rapid transport, seek, key/speed automation and stop on EOF.

**Instrument:** callback execution percentile/time budget (sample rate/block), underruns, producer backlog, ring high/low-water, deck generation drops, deviceEpoch mismatches, decoder errors, worker CPU/memory, frame/time alignment and buffer crossfade clicks. Favor off-callback telemetry counters and test-time assertions over callback logging. Asset/license scan and exact SHA manifests.

**Dependency decision:** JUCE reader/transport primitives are tools, not a guarantee that realtime callbacks never touch disk; use the explicit ahead-of-time ring design. Signalsmith Stretch is MIT, source [license](https://github.com/Signalsmith-Audio/signalsmith-stretch/blob/main/LICENSE.txt), good candidate but **selection remains provisional** until instrumented A/B and cross-platform validation. If new third-party library is vendored, pin source commit+hash, satisfy copyright notice, include reproducible build and audit distribution license compatibility with repo `AGPL-3.0-only`. Check per-OS media codec support during build (JUCE formats can be compiled conditionally), do not promise unverified MP3 on all platforms.

**CI rule:** docs-only PR: review diff and links; no expensive CI. First code milestone: fast C++/JS contracts; only then one necessary exact-SHA native and/or UI. Request FAT only when user needs a real playable installer; hardware listening never claimed by CI.

## 10. Architecture decisions and explicitly rejected approaches

- **Selected:** authoritative native deck + worker decode/time-stretch → preallocated PCM ring → virtual source binding → existing ChannelId graph; one GUI control/readback.
- **Rejected:** WebAudio/HTMLAudioElement, an embedded mini-app, routing music through virtual loopback ASIO input, `PLAYER` as a fake VST3, decoder called in audio callback, playlist state inside hardware binding, default autoplay, reusing source ordinal `-2`, UI-only fake meters.
- **Deferral:** dual deck + beat grid + stems + automation markers can use the same source contract later. Don't implement abstractions that force a second renderer early.
- **Open decisions requiring measured prototype, not preference debate:** time-stretch quality/CPU on 64-frame ASIO systems; optimal ring and DSP fade length; exact player meter tap (pre-fx vs post-fx vs both); last-output resume policy under audio device disarm; codec availability across OS.

See [implementation workpack](workstreams/ARSON_PLAYER_IMPLEMENTATION_WORKPACK_20261109.md) for the order, exact stop/go acceptance and agent instructions.
