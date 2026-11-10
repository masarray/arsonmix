# ArSonMix — Fast, Safe Feature-Extension Playbook

Status: WORKING PLAYBOOK, v1 2026-11-11. Root [AGENTS.md](../../AGENTS.md) and [execution blueprint](MAINTAINABILITY_EXECUTION_V1_20261111.md) take precedence. This is the route to implement new capabilities without heavy repeated architecture analysis.

## 60-second feature classification

| User asks for | Inspect these first | Must NOT add |
| --- | --- | --- |
| Built-in FX (EQ, compressor, harmony, spatial/throw) | engine/builtin_fx/BuiltInFxCatalog.h, corresponding Channel*Processor.h, src/FactoryFxRegistry.h, relevant FX slot/lease code, existing tests | second callback/graph, duplicate generic DSP host, per-plugin device handler |
| External vendor VST3 host behavior | src/PluginCatalog.h, src/LiveAudioEngine.h, engine/core/{LiveChannelFxGraph,DynamicChannelFxLeaseBank}.h, src/NativeMixerSessionFxRestore.h | processor create/destroy in callback, unverified plugin state capture, duplicated editor lease |
| Channel / BUS / WIRE control | engine/core/{DynamicChannelRoster,DeviceIndependentMixerSession,DynamicNativeSessionPublisher}.h, src/NativeLogicalMixerWorkspace.h, integration/ApprovedNativeControlIntent.h, routing tests | strip index as persistent ID, hidden admission flow, optimistic JS graph, default device reset |
| Native GUI control / FX editor interaction | web/approved-native-contract.js, web/approved-native-bridge.js, scripts/build_approved_native_gui.py, current assembled /preview/, associated JS/Chromium tests | independent GUI renderer, mock PCM, second subscription, a new public preview |
| Audio I/O / ASIO / WASAPI | engine/device/**, src/LiveAudioEngine.h, Main device apply boundary, device and mixer session tests | second JUCE AudioDeviceManager, implicit different physical output, callback waiting on secondary stream |
| ArSon Player source | engine/player/**, DynamicPreparedMixerBlock.h, Player virtual PCM tests, player native commands | independent audio output device/callback, auto-Play on launch, reset existing channel |
| Session / Show / backup | src/NativeMixerSessionDocument.h, NativeMixerSessionStore.h, NativeMixerSessionSaveWorker.h, relevant Show schema/migration tests | persisting device pointers, forcing fixed 10-strip truncation, reinitializing vendor FX state on reopen |
| GUI aesthetics / WIRE physics | ONLY actual approved /preview/ generated source and exact GUI test/provenance path | rewriting native audio or replacing owner-approved controls |

**Before writing code:** check LIVE main, active PRs and actual installed GUI build inputs. Confirm the requested feature's ownership and actual native PCM route. Avoid reading 200 historical docs or proposing a new framework when current seam works.

## Per-feature implementation checklist

1. **Specify one user gesture and exact outcome.** Example: "Insert FX on CH07 -> FX loads prepared instance -> follows CH07's existing WIRE route to MAIN, editor opens, sound changes; no new monitor step."
2. **Reuse existing identities and clocks.** ChannelId, EdgeId, stable FX slot/lease ID, device epoch, graph revision, requestId. Snapshot/ACK carries only native accepted truth.
3. **Implement in owning domain.** DSP stays in processor; session mutation in session; hardware binding in device; audio publishing in graph; host transport in one control handler. Avoid unrelated Main device/Player code.
4. **Support failure before success.** Unsupported plugin, invalid state, duplicate/stale request, device offline, scan busy, bus cycle, failed preprepare, and missing saved vendor DLL must leave accepted graph/document intact.
5. **Add nearest tests first.** One unit/PCM fixture for domain behavior, one host/ACK/readback fixture, browser smoke ONLY if GUI changes; do not add all tests to every CI job.
6. **One meaningful PR and bounded CI.** Fast targeted on PR, exact-SHA UI if GUI changed, native OR fat if native changed, real hardware FAT separately when needed. Record changed files and failure/rollback.

## Recipe A — new built-in FX

Define catalog identity and stable insert/slot semantics once. Implement DSP as its own processor with deterministic prepare, bounded process, state serialization and bypass/latency reporting. Register compiled processors in `src/NativeBuiltInFx.h` for the generic live insert, offline prepare, restore and native picker. Preserve the same editor/ACK readback and rack bypass mechanics as EQ/Compressor/Harmonice/Vocal Throw. Channel/BUS routing sees only generic FX lease, not the plugin brand. Verify zero/known latency/PDC, offline state, inserted audio, bypass, reorder/move and device restart. Keep plugin-specific UI ONLY in its editor component, not device setup.

**Design signal:** needing to modify a large per-plugin switch in Main, engine callback and browser bridge for every new audio processor indicates incomplete M4 host-seam consolidation. Solve shared host responsibility once and remove old branch at the same time. Do not add a new wrapper if only one plugin needs a unique capability.

## Recipe B — add a channel or WIRE feature

User intent -> native control transaction on stable IDs -> validate destination role, cycle, FX capability, SOURCE ONLY and existing source->MAIN edges -> prepare immutable graph -> publish atomically -> ACK, then accepted new graph readback. For no hardware, commit only logical document and show nonlive state; deferred physical binding must retain all edits. Physical source names and sparse pins are never inferred from browser ordinals.

**Regression:** CH05 input -> FX -> MAIN; SOURCE ONLY preserves CH signal, silences MAIN; retarget/remove/reconnect; BUS FX -> MAIN; PARALLEL SEND not duplicated; rejected cycle leaves old audio unchanged. Device reconnect must not erase channel or wire.

## Recipe C — add a GUI or transport command

Contract version and typed payload -> one intent mapping -> native validation (requestId, expected doc/graph/device revision) -> ACK/negative ACK -> separately verified snapshot readback -> update existing /preview/ DOM. Never immediately paint the requested value as permanent. A visible busy/loading state may appear while Apply is in flight; it must correspond to an actual native transaction, not a fake completion.

**Regression:** event once; duplicate/stale rejected; status change survives snapshot; older UI compatible; a rejected action does not repaint the old graph. Check assembled GUI output before editing any HTML/JS seed.

## Recipe D — ASIO/device/restart change

First assert retained document and VST leases exist independently of device. Preflight exact driver/ports, suppress unsafe output during transition, keep existing state intact until final graph binding, and rollback without clearing accepted edits. Known verified hardware may resume with one natural operator gesture; new output requires user choice. Never use WASAPI secondary stream backpressure to pace realtime ASIO.

**Regression:** failed open, stale generation, same-driver reopen, new-driver missing pins, sample rate/buffer restart, source NONE->mono->stereo, no auto-unmute of intentionally muted MAIN; physical witness remains mandatory.

## Recipe E — Player and virtual audio

Decode/relink on worker, publish bounded PCM ring, adapt source into the canonical ChannelId input and existing FX/MAIN graph. Exactly one hardware output callback. The user's explicit Play initiates trusted output; not auto-playing or starting a second device on launch. Preserve user’s SOURCE ONLY and existing BUS routes.

## Quick diagnosis by symptom

| Symptom | First evidence to inspect |
| --- | --- |
| FX picker appears but no actual insert | native intent + ACK + accepted FX lease/readback + active graph; never treat browser picker success as audio |
| UI looks functional but silent | LOGICAL_ONLY vs installed; device epoch/pins, source, MAIN routing, output safety, channel meter vs MAIN meter |
| WIRE "not admitted" | accepted graph revision/epoch + installed producer, not cosmetic WIRE drawing |
| Session disappeared on device swap | .smix authoritative document vs ephemeral DeviceBinding, transaction rollback |
| VST editor wrong plugin after MOVE | stable processor instance/slot lease and editor owner, not UI ordinal |
| Crackles with 0 FX | input/output buffer discontinuity, independent clocks and callback schedule, not just DSP CPU |
| Code change alters /preview/ | deterministic source assembly, button assets, WebView bundle hash |
| New feature touches 7 unrelated modules | missing ownership seam; create one focused shared adapter rather than another special-case path |

## Definition of a lightweight extension

For a normal feature we expect a recognizable single-domain code owner, a small number of related touched modules plus tests, no extra PCM callback, no duplicate GUI/device model and clear test selection. This is a *review heuristic*, not a rigid maximum-file count: real cross-layer user workflows can legitimately touch engine, bridge, UI and persistence together.

Never declare a latency, realtime CPU, user satisfaction or release improvement without comparing real measurements on the same setup.
