# Auto Ducker — operator UX and workflow contract V2
Status: design admission contract, not production UI. Supersedes the single physical-source UX in AUTO_DUCKER_PRD_ARD_TRD.md; that file records the existing candidate, not the desired product. PR #964. Do not expose in picker until native gates below pass.

## Product decision and evidence
- Yamaha dspMixFx: https://manual.yamaha.com/audio/apps_software/dspmixfx/en/03_effect_en.html — multiple trigger signals, streaming mix post-fader, dedicated Ducker controls.
- Yamaha DM3: https://manual.yamaha.com/pa/mixers/dm3/rm/en-US/6346142603.html — target dynamics, Other Ch/MIX key source, separate key meter and cue.
- Allen & Heath Qu: https://www.allen-heath.com/content/uploads/2023/06/Qu-Mixer-Reference-Guide-AP9372_10.pdf — Single/Gang workflow. Do not copy its group limits.
- OBS: https://obsproject.com/kb/compressor-filter/ — effect on target, explicit sidechain selector.
- FabFilter: https://www.fabfilter.com/help/pro-c/using/sidechainsection — progressive disclosure of detector controls.
- RIVAGE: https://manual.yamaha.com/pa/mixers/RIVAGE_PM_series/en-US/10778164491.html — fixed attenuation above trigger threshold.
These are documented workflows, not comparative listening tests. Use original ArSonMix visual identity, not vendor artwork.

## P0 golden path
1. On ANY compatible channel's existing FX rack, click empty slot → Dynamics → Auto Ducker (search aliases: duck, sidechain, talkover). After native insert ACK, open exact instance's existing owned FX window. Never route through physical I/O dialog.
2. Header shows target name, channel ID context and slot; target is NOT editable from the local insert editor. Initially enabled but unassigned = unity, No trigger selected. Never auto-select a mic based on name or slot.
3. Select one or many logical channels in a searchable source popover, grouped by actual roster roles. Source list displays name and channel context, checkboxes, actual detector level and availability. Keep selections while filtering. Apply once commits an atomic full set; Cancel/Escape leaves accepted selection unchanged. Show self/cycle/unsupported rows disabled with reason; never hide a previously selected missing source. No physical pin names as primary identity.
4. Default preset Radio Voice-over (-34 dBFS threshold, 12 dB reduction, 18/220/480 ms attack/hold/release, 5 dB hysteresis, voice-band HP120 LP5000). Presets are settings-only, do not change trigger IDs, target, slot, tap, bypass or routing. Apply settings as one accepted transaction. Custom state if parameters change.
5. Speak: source meter crosses threshold, active source highlighted with text, status transitions Attacking/Ducking/Holding/Releasing/Ready, gain reduction meter reads effective actual audio, target fader stays still. No separate Start, Apply Audio, device restart or silent extra route.
6. Change amount, threshold and timing live through accepted parameter ACK. Hover/help explains units and meaning. Reopen restores exact settings and selection. Save Show uses acknowledged state. Rack slot shows compact GR/status if actual live telemetry is available.
7. Add same FX to another track for independent controls; copy settings without implicit key rebinding. To lower several tracks identically, use existing group bus with one insert; grouping remains explicit audible routing. Named trigger sets and linked-target groups are P1, not required for the basic path.

## Layout contract
Desktop compact floating editor, proposed 720×450 logical px, minimum 560×380 with internal scroll and responsive two-to-one column transition. Native chrome and theme tokens from existing approved editor. 12–13px labels, 14px title, 30–32px targets; restrained graphite, teal for actual activity, amber for actionable partial/waiting state. No oversized cards, decorative waveforms or giant typography. Header target breadcrumb + local bypass + preset. Left: selected sources with meter, add/edit action. Right: effective GR, threshold and depth. Lower: attack/hold/release row. Advanced collapsed: per-source tap (default post-fader, alternatives post-FX/pre-fader and post-trim/pre-FX only when native supports them), HP/LP, hysteresis, per-source sensitivity P1. Separate key meter dBFS from GR dB. No history trace until sampled real telemetry; static timing diagram must explicitly say response illustration.
Use native-accessible buttons, labels, ranges with numeric entry, keyboard arrows, Tab order, clear focus, accessible names and status text; avoid color-only semantics. Popover focus returns to opener. Escape dismisses popover only, then existing native window semantics. Meter updates must not rebuild source rows or steal focus; throttle 20–30 Hz and suspend hidden window; mark offline/stale rather than fabricated values. On narrow layout keep all controls accessible; no horizontal scroll. DPI checks 100/125/150/200%.

## Authority and safety
- Use approved floating FX bridge and rack view model; one owner keyed by workspace/device epoch, revision, ChannelId, slot, exact instance token, never name/ordinal. Same instance refocuses; unrelated VST opening cannot close/destroy it. Move follows same retained instance only; replacement/deletion invalidates old editor.
- Native source inventory and tap admission are authoritative; source set validates together against graph and combined audio/sidechain dependencies. Reject self, cycles, duplicate IDs, incompatible stages or budget overflow with actionable reason and no graph mutation. MAIN containing its target is a common cycle. Never substitute an unadvertised raw tap or previous-block read for requested post-fader. Topological staging, PDC and cycles require separate native qualification. No extra latency promised beyond zero gain-cell latency.
- Source selection uses OR/MAX of independent per-source detection, never PCM sum; overlapping talkers do not multiply depth. Missing/muted/solo-excluded source contributes nothing, others continue. Zero available sources = smooth return to unity. Distinguish no source selected, disconnected, muted, below threshold, unsupported, pending and offline. Deleted source remains visible as missing ID until user edits; never rebind to same label.
- Local bypass button controls only slot; effective bypass also reflects rack/global. Show exact blocking reason, keep settings. No automatic unmute. Preset, source change, close, reload and offline edit cannot assert a live ACK.
- Edit transaction: accepted snapshot + draft + pending request ID; render pending indicator while old audio remains authoritative; successful ACK and matching current instance/revision apply; rejection retains accepted settings and draft for correction, timeout requests fresh snapshot, late ACK from retired instance ignored. One structural source edit in flight; continuous scalar updates coalesced without losing final value. Offline editing stores logical state, meters unavailable, activation separately admitted.
- Preserve existing MAIN/monitor/stream routing; target FX affects its downstream sends according to insert location. If the operator wants stream-only ducking, use an existing separate stream bus if supported, never imply all hardware outputs are automatically separate.
- Manual talkover and key audition are P1 (monitor-only cue, no unintended MAIN/stream routing). Spectral mode P2. Not in P0 UI. No semantic VAD claim.

## QA acceptance gates (all required before picker visible)
| ID | Scenario | Expected |
|---|---|---|
| UX01 | Cold add on Music or arbitrary compatible input/group/return | Exact editor opens after ACK; no source selected, unity |
| UX02 | Select Host and Guest in filtered list, cancel then apply | Cancel preserves accepted set; apply one atomic transaction |
| UX03 | Rename/reorder two keys; same-name different IDs | Identity retained, new labels; no wrong channel |
| UX04 | A speaks, B joins, A stops, B stops | One bounded reduction, seamless hold/release |
| UX05 | Mute/disconnect A while B active | B still drives; A clear status; all missing smooth unity |
| UX06 | Select self or MAIN downstream or mutual cycle | Disabled or native reject, reason, no routing mutation |
| UX07 | Change preset/amount then reopen and save Show | Accepted values persist; keys and tap unaffected |
| UX08 | Local/rack/global bypass in all combinations | Correct reason and effective GR; slot preference retained |
| UX09 | Open VST, move FX, delete or replace slot | Same owner retained only; stale editor cannot write |
| UX10 | Offline/reconnect/repatch/late ACK/timeout | No fake meters or silent substitutions; recover by ID |
| UX11 | DPI 100–200%, keyboard only, 40+ channels | All controls reachable; searchable list stable under telemetry |
| UX12 | MAIN + monitor + VB Cable capture | Actual output matches topology; no duplicate FX/callback |
| UX13 | CPU + listening A/B speech/music and overlapping mics | Natural phrasing, no pump/click/dropout; measured CPU |
| UX14 | Full graph cycles/PDC and native state roundtrip | Deterministic current sample timing, no hidden block delay |
Design review prototype is not PCM or installer proof. Current branch has one known failed logical-mixer test (nonfinite key isolation, run 45927919916), and six other workflows passed at prior head. Do not mark ready until resolved and exact-head qualified. Current raw pre-trim candidate does NOT meet the post-fader multi-source requirement.

## V3 hardware panel revision
Retain target/source and ACK protocol. Use the same stationary compressor knob-body and independent rotating pointer SVG assets with a 270° progress arc; five core controls in a single physical control deck, recessed detector and gain-reduction LCD, teal source activity and amber gain reduction. Drag vertically, Shift fine, double-click default; accessible numeric editing remains. The source picker has explicit accessible source labels and stable data IDs. Screenshot evidence must be captured before assertions as well as after accepted source edits. Hardware revision is candidate-only until visual and native checks pass.
