# ImerSip v2 — wide musical concert sound with a protected vocal center

Status: **field research, physical headphone A/B pending**. Replaces acoustically rejected ImerSip v1. The user liked some SonKu P8.2 configurations with virtually wide speakers near 180°, but rejected the ImerSip v1 field ZIP as nasal/muffled and stereo-to-mono, without any editor. Do not use the earlier green DSP tests as a certificate of listening quality.

## Evidence and diagnosis
- v1 hard-coded `stereoSpreadDegrees=60`, put the **entire stereo mix including vocals** through the P8.2 center HRTF and six-wall reverberation, then applied a small high-SIDE shelf. P8.2's protected anti-phase SIDE could survive mathematically while the direct music MID already sounded colored and the scene was focused in the head.
- v1 tests compared the output MID to the **same colorized P8.2 core**, instead of the music's original MID. Those tests were incomplete for vocal coloration.
- `ImerSipProcessor::hasEditor()` had returned false, and `createEditor()` returned nullptr. This was a shipped UX omission.
- A nearly-180° parameter on P8.2's P7.5 **music-safe** renderer controls side width and reflections; P8.2's direct FIR still uses one centered HRTF. Do not claim a real 175° virtual loudspeaker pair without independent spatial validation.

## v2 architecture
- One existing native JUCE stereo FX insert, unchanged `armix.builtin.imersip.v1` identity and versioned IMRS 36-byte v2 state with backward-compatible 32-byte v1 restoration (Detail=OFF on migration).
- Capture ORIGINAL source MID=(L+R)/2 and SIDE=(L-R)/2, process the same original stereo samples in one existing P8.2 research core, retain **only the differential SIDE component** from that processing, mixing a bounded portion back into ORIGINAL SIDE. **Original MID is never re-rendered through center HRTF or early/late room**. The DSP then applies an optional coherent positive LF shelf and positive highpassed SIDE shelf, a small positive >5 kHz Detail shelf (identical L/R coefficients), and a single output trim.
- With BASS=0, DETAIL=0 and gain=unity the final MID must match the original source MID within float arithmetic tolerances at 44.1 and 48k, for mono vocal, hard panned, arbitrary stereo, and anti-phase source. The original SIDE is always present at unity before optional enhancement.
- One fixed-front source; stage WIDTH maps 80–180°, default 175°; room amount and wet-side return are bounded to prevent the foggy/full-band effect of v1. No moving/revolving source, Haas delay, polarity-flip widening, stereo-to-mono preprocessing, compressor or global makeup gain. The P8.2 core's source code and public SonKu behavior remain untouched.
- A truly anti-phase **original** stereo recording mathematically cancels in mono. ImerSip cannot preserve both original L and R and eliminate that fact; the enforceable promise is avoiding **additional** destructive center cancellation.
- Real externalization depends on direct HRTF/BRIR, individualized spectral details, motion and reflections and is not guaranteed by dry MID protection or measured source angle alone. This is a bounded, favorable audio A/B candidate; treat next personalized speaker externalization as a separate listening-driven improvement, not a validated feature.

## UX/controls
`ImerSipProcessor::hasEditor()==true` and `createEditor()` now return an 840x474 native JUCE panel hosted in the existing mixer FX editor window (no separate WebAudio/ASIO authority). Compact premium dark styling, visual listener + L/R stage; Concert Hall / Cinema / Live Room; Immersion / Stage Width / Venue Space / Bass Impact / DETAIL (>5 kHz only) / Output gain, with numeric readouts. Detail is a bounded +0..1.3 dB stereo-coherent high shelf; no 1–4 kHz vocal EQ boost.

Changes enqueue using the existing SPSC real-time mailbox; only audio callback applies them, with completion readback to the editor. During drag the editor batches to one final action instead of pushing every frame. Store/restore remains with the native mixer Save/Open IMRS state. The host no longer needs JS specialty per-plugin commands for ImerSip, unlike SonKu's existing custom browser editor.

## Qualification gates
1. Source C++ PCM at 44.1/48 kHz verifies **original** MID parity, L/R SIDE preservation, mono vocal, anti-phase, high-spread geometry, genuine P8.2 room residual, bass 40/70/110/160/220 Hz and chunk invariance.
2. True Windows JUCE build and native CTest must instantiate **a working editor**, plus descriptor/picker, audio-applied ACK, save/restore and PCM. Build provenance explicitly marks UI and dry-vocal protection.
3. On the owner's actual music/headphones: level-matched A/B original bypass vs P8.2 GOLDEN vs ImerSip v2; check vocals, midrange color, bass, high-frequency harshness, width, front/external distance, localization and CPU, including ROOM OFF/ON. If perceived stage is still in-head, stop and investigate independent left/right BRIR externalization before claiming acoustic success.
4. Do not merge/release as public or replace the latest main GUI; research branch is stacked on #695 and #706 and can be ported when approved.

### Science/research basis
- Sound Externalization: A Review of Recent Research, https://pmc.ncbi.nlm.nih.gov/articles/PMC7488874/ : interaural coherence and room interaction, strong caveat about nonindividualized HRTF and frontal localization.
- The role of reverberation-related binaural cues in the externalization of speech, https://pubmed.ncbi.nlm.nih.gov/26328729/ : frontal sound requires plausible binaural reflection cues.
- Auditory localization: a comprehensive practical review, https://pmc.ncbi.nlm.nih.gov/articles/PMC11267622/ : HRTF, relative motion, reverberation and tradeoffs.

**Do not claim dearVR equivalence, full phase cancellation elimination, physical spatial externalization proof or fully qualified monitoring without actual user's listening acceptance.**
