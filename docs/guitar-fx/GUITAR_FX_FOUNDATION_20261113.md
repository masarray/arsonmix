# ArSonMix Guitar FX Rig — product contract, UX, assets, and native architecture

Status: **foundation/design only** (2026-11-13). Not installed in ArSonMix, not a working amp simulation, and not evidence of ASIO/PCM. This is an implementation contract for one coherent vertical slice, not a new approval gate.

Baseline: `main@94f9b6a2d1de81c9e3dab4978aaf2d44dcf7870e`. Read the **latest** main and active PRs again before implementation. Existing unrelated PRs at authoring: #605 (picker opens editor), #604 (GAIN/FX Rack), #601 (8D SonKu), #521 and #512. Do not overwrite their work.

## Product outcome

An ordinary guitarist selects the verified Hi-Z input on a normal ArSonMix audio channel, chooses **Add FX > Guitar FX Rig**, selects a usable tone, and plays through existing FX/channel/WIRE/BUS/MAIN to an explicitly verified hardware output. **No independent audio device, no audio callback, no second mixer, no Monitor/ARM wizard.** Guitar FX is one first-party native insert with an internal modular effect chain.

The initial playable target is **guitar instrument -> gate -> drive -> amp model -> cabinet IR -> channel fader -> WIRE -> MAIN**. Compressor, modulation, delay and reverb can be added/bypassed within the same insert; the accepted chain is bounded and stored in the FX's versioned state. It remains compatible with ordinary external VST3 inserts elsewhere in the rack.

## User-facing workflow — no guessing

1. **Insert:** on a verified audio channel, open normal FX picker, select *Guitar FX Rig*. Native insertion is accepted only after a real processor lease + same-revision snapshot; when loaded, open the editor using the existing generic editor-target path (reconcile #605). If no hardware exists, retain LOGICAL_ONLY and mark audio unavailable; do not display live meters.
2. **First sound:** load the *Clean Studio* starting preset. With an authorized output and source/WIRE route, the guitarist hears a full-range-ready tone; cabinet IR active by default. Input clip indication shows a genuine DSP peak, not simulated data.
3. **Tone selection:** one-line top toolbar has category-filtered preset search, previous/next, favorite, A/B snapshots and a real tuner overlay. Presets may be previewed by selection; modified/dirty state is visible. No destructive preset overwrite.
4. **Signal flow:** always-visible compact horizontal chain. Click a block to focus the **one** large editor below; click its power indicator to bypass that block; drag movable pedals to reorder. Amp/Cab are paired/guarded in V1; illegal drops rejected without altering accepted chain. Per-block change is native-ACK/readback driven in production.
5. **Focus editor:** when AMP selected, show a realistic *amp head only* with six tactile knobs and model selector. When CAB selected, replace amp illustration with *cabinet/IR* view and microphone/IR controls. When pedal selected, show that pedal with its appropriate parameters. **Do not show amp and cabinet permanently side by side** or duplicate a second redundant tab bar below the signal chain.
6. **Live use:** tune via overlay (audio may be temporarily muted by explicit tuner choice), change A/B snapshot intentionally, toggle all FX with the existing rack bypass (not a second bypass ownership). Input/output meter and clip are slim; CPU/latency diagnostics are accessible without dominating screen.
7. **Save/reopen:** FX state persists through existing `.smix` and existing user-preset mechanics, using a versioned FX blob. Device reconnect retains model identity, IR reference and pedal chain; missing model/IR reports a recoverable error and keeps the previous accepted state wherever possible.
8. **No misleading UX:** a browser-only design study must say *no audio*; actual browser /preview/ is not PCM authority. No instant green LED/processed meter before native confirmation.

## Visual system and original PNG assets

Hardware realism uses original *decorative* multi-resolution raster PNG layers in `design-references/guitar-fx/assets/`. The generator at `scripts/build_guitar_fx_assets.mjs` is deterministic and needs no third-party image, copyright-protected brand logo or runtime canvas. The currently committed hardware skins are original procedural drawings, not product photographs or third-party bitmaps. Their realism must be visually reviewed before production promotion.

**Never embed labels, values, preset names, focus indicators, hit areas, LED logic, or translated text inside PNGs.** Render these separately in HTML/CSS over the artwork, using native ACK/readback as truth. Hardware art is not the control source of truth.

| Role | PNG | Runtime behavior |
|---|---|---|
| Amp shell | `amp-head-class-a.png` | Original tolex and brushed faceplate. Model name, knobs, power and mode are overlays; future amp variants are skin entries, not new host controls. |
| Cabinet shell | `cabinet-dual-12.png` | Grill/speaker drawing, mic position is a separate interactive overlay and **not** a claim that a real mic image changes DSP. |
| Pedal chassis | `pedal-gate.png`, `pedal-compressor.png`, `pedal-overdrive.png`, `pedal-delay.png`, `pedal-modulation.png`, `pedal-chorus.png`, `pedal-reverb.png` | Shared chassis geometry. Gate, compressor, chorus and reverb each have original themed PNG skins; parameter schema is separate. |
| Shared control | `knob-brushed-metal.png` | Rotate the same original asset based on parameter value; visible value and keyboard accessible input are separate. |
| Status lamps | `led-active.png`, `led-bypassed.png` | Visual state after accepted snapshot, never optimistic mock PCM. |

Asset design rules: preserve intrinsic aspect ratio; use `object-fit:contain` for hardware images; no dynamic raster per frame; load each raster only once; do not scale the same knob differently in ON/OFF state; target 1×/2× available by responsive CSS first and only introduce true additional exported sizes if measured blur requires it. Screen readers must access actual parameter names/values; reduced motion and keyboard focus cannot depend on decorative images. Avoid heavy CSS filters and moving full-canvas shadows.

**Asset provenance gate:** each PNG must parse as valid PNG, be indexed+transparent (when applicable), non-empty and within a recorded byte-size budget; builder output must be byte-identical across runs. Replacing the image is a design review, not a hotfix to DSP.

## Approved in-product editor anatomy

- Row 1, compact **utility toolbar** (preset / search / favorites / A/B / Undo / tuner) and truthful IN/OUT levels, no new host device selector.
- Row 2, one **signal chain** that communicates order, current focus and block bypass distinctly. Fit into width; if narrow, chain scrolls horizontally with visible directional affordance rather than shrinking labels below legibility.
- Row 3, one **large focused hardware editor**. Preset browser is collapsible; stage and cab pages are alternatives, not simultaneously permanent views.
- Row 4, slim **status footer** with accepted channel identity and route readback; never claim an arbitrary 'GUITAR IN 1 → MAIN' route if the installed ChannelId/EdgeId graph says SOURCE ONLY.
- Editor opens in a movable/resizable native floating workspace if the generic existing FX editor supports it; preserve owner-approved mixer buttons and `/preview/`.

For developer-only interactions see `design-references/guitar-fx/editor-prototype.html`. It is **not** a second published mixer, native editor, or audio producer; do not copy any of its fake values into `/preview/` as live status.

## Native module and ownership boundaries

```text
approved /preview/ FX picker & existing editor host
    -> existing web native bridge (typed request, not DSP)
    -> NativeFxControlRequest / accepted instanceId, ChannelId, epoch, revision
    -> NativeBuiltInFx registry: armix.builtin.guitar-rig.v1 [only after processor exists]
    -> GuitarRigProcessor : juce::AudioProcessor
         -> immutable GuitarRigProgram / bounded ordered ModuleDescriptor[]
         -> Gate / Dynamics / Drive (native DSP)
         -> AmpModelAdapter (NAM runtime on prepared compatible model)
         -> CabinetConvolver (prepared IR)
         -> Mod/Delay/Reverb (bounded native DSP)
    -> existing DynamicChannelFxLeaseBank and single installed native PCM graph
    -> existing channel WIRE / BUS / MAIN, ASIO device binding
```

**Files to extend only at integration time:** `engine/builtin_fx/GuitarRigProcessor.h` and focused DSP helpers; `src/NativeBuiltInFx.h` (catalog); existing accepted FX state/readback/restore and inspector editor; existing assets bundling recipe; focused PCM and browser tests. **No new callback**, graph publisher, ASIO manager, JS PCM path, VST3-wrapper process, or copied Main.cpp FX switch. Consult `docs/architecture/FEATURE_EXTENSION_PLAYBOOK.md`. Harmonice is a *separate vocal harmony processor*; never reuse its identity for Guitar FX.

### Prepared DSP and realtime budget

All NAM models, IR files, resampling tables, filters, buffers and session states load/validate/allocate on CONTROL or worker threads before an atomic processor/graph handoff. PCM callback must be bounded, no allocation, blocking, disk/network access, logging, VST3 instantiation or IR/model loading. Fixed-capacity circular buffers and parameter smoothers are per-instance. Bypass retains state and uses click-managed transitions; switch program by bounded safe crossfade and retiring old graph on CONTROL. Reject unbounded/malformed neural models and IR lengths before preparation.

Start at 48 kHz / 128-sample ASIO with live cabinet active. **Never label buffer duration as round-trip latency.** Measure CPU/xrun and measured hardware round-trip latency with exact model/IR/sample rate in FAT. High-resolution/oversampling modes must declare actual PDC and explicitly cap processor time. Model and IR licensing require separately reviewed distribution; NAM runtime licensing does not automatically approve bundling third-party capture files.

### Versioned program data (proposed contract, NOT installed)

```json
{
  "schemaVersion": 1,
  "programId": "factory.clean-studio.v1",
  "blocks": [
    {"id":"gate-1","type":"gate","enabled":true,"params":{"thresholdDb":-60}},
    {"id":"drive-1","type":"drive","enabled":false,"params":{"gain":0.3}},
    {"id":"amp-1","type":"amp","enabled":true,"modelRef":"verified-model-id"},
    {"id":"cab-1","type":"cab","enabled":true,"irRef":"verified-ir-id"}
  ],
  "focusedBlockId": "amp-1",
  "snapshotA": {},
  "snapshotB": {}
}
```

The persisted DSP state is independent of what is focused/expanded in the editor. Block identity is stable; reorder affects IDs, not processor lease identity. `programId` never substitutes an unverified file path. Do not store absolute transient hardware device paths in the program. State migration must be tested for V1→V2 changes. Snapshot switching is user intent with monotonic revision, not an uncorrelated JS-only optimistic mutation.

## Milestones and acceptance

**Foundation F0 (this branch):** UX contract, original PNG sources+rendered bitmaps, manifest, local interactive editor study and asset contracts. No audio source modifications and no user-facing new FX picker until a real processor is ready. Inspect diff, asset verifier and local editor. No Windows/Chromium heavy CI for design-only work.

**M1 playable vertical slice:** ONE real native GuitarRigProcessor with gate, overdrive, a usable amp model and cabinet IR; approved FX picker/host editor, generic typed ACK, audio through ChannelId / WIRE / MAIN, valid .smix restore and safe missing-IR behavior, exact-SHA native tests and real ASIO FAT. A compiled stub with silent/pass-through audio does **not** pass.

**M2 modular performance:** per-block reordering, modulation/delay/reverb, tuner, A/B snapshot, more factory presets, efficient no-pops transitions, focused editor, accessible controls and real source-vs-output meter. UI integration must reuse the existing editor and preserve /preview/ authoritative mixer.

**M3 quality/reliability:** varied licensed NAM models and IRs, measured amp/cab profiling, MIDI foot control, resource limiter, 44.1/48/96k + 64/128/256 sample device tests; still one native callback. No release claim without witnessed ASIO guitar-input FAT.

### Non-negotiable tests

- golden-PCM: guitar -> amp/cab changes frequency/harmonic content; real wet chain vs bypass and output gain matching; no NaN/Inf/denorm problems, no silence with valid input;
- offline: malformed/too-large/unsupported model, corrupt IR, missing file on restart, stale device epoch, duplicate command, stale block ID, invalid/multiple block reorder;
- routing: CH05 direct IN→GuitarFX→MAIN, SOURCE ONLY keeps channel meter but MAIN silent, BUS + FX, mono/stereo device switch, external VST3 elsewhere still works;
- RT: zero dynamic allocation/disk/locks inside processBlock, bounded DSP, accurate known PDC, deterministic clean bypass, no dangling retirement;
- editor: keyboard/mouse/touch knob, focus vs bypass distinction, preset browsing, no false live meter, width-reflow, reduced motion, accepted snapshot after every native edit;
- licensing/packaging: original PNG provenance, approved model+IR license/sha, rebuild reproducibility, assets bundled offline.

### Trade-offs / explicit non-goals

- Do not simulate mic distance or speaker blend in DSP solely because the decorative cabinet UI has a microphone circle; expose only working controls.
- Do not conflate premium photorealism with cloning trademarked hardware logos or screenshots.
- Do not implement an exhaustive Helix-like graph/router in M1; reserve a bounded model for later parallel paths.
- Do not fork ArSonMix into a separate DAW or create another public `/preview/`.
- **Realistic art needs visual iteration with the user**. The initial procedural asset pack is a lawful, reproducible starting set, not yet the finished photo-grade visual target.
