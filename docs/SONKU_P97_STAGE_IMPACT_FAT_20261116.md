# SonKu P9.7 Stage Impact and External Presence — PRIVATE FAT, 2026-11-16

**Selected listening checkpoint:** P9.6 PR #727 head `720cec619b41aa601414d4b5ae664cb418f04ca1`. User confirmed P9.6 is already reasonably good; preserve it immutably for A/B. P9.7 is a child branch and does not rebase P9.6, alter `main`, merge the competing P9.5 PR #724, or change old .smix bytes.

## Research, limits, and actual algorithm

- [Sound Externalization: A Review of Recent Research](https://pmc.ncbi.nlm.nih.gov/articles/PMC7488874/) describes the significance of direct pinna cues, realistic interaural differences in reverberation, and room/source congruence. Real externalization requires headphone listening and cannot be proven by spectral gain or generalized KEMAR only.
- [Jiang et al., effect of pinna filtering](https://doi.org/10.1016/j.apacoust.2020.107257) finds direct pinna filtering helps externalization whereas applying it to diffuse reverberation can reduce externalization. We retain the measured direct HRIR and existing non-pinna image-source ER path.
- [AES virtual bass review](https://aes.org/publications/elibrary/elibrary-page/?id=18050) documents perceived bass versus distortion risks. We DO NOT generate missing-fundamental harmonics, synthesize extra LFE, or attach a headlocked mono bass.

One opt-in **native P9.7** component (`SonKuP97StageImpact.h`) adds three bounded paths to the existing P9 mode-9 scene only when ROOM is ON and distance is above 2m. All parameters are research constants, not hardcoded .smix fields or hidden GUI pseudo-controls:

1. **Concert punch / room body:** prepared, low-Q ~85Hz bandpass at +0.23 relative peak to the already HRTF/P9.6-processed direct ear pressure; one-pole 62Hz sub body at +0.085. The existing P9.2 position-aware damped 3-mode low-frequency *room return* is modestly increased by at most 12%, without modifying resonator decay or creating uncontrolled feedback.
2. **Center-panned presence (not vocal separation):** low-Q ~1.9kHz bandpass of INPUT L+R MID with at most +0.19 on both virtual speaker source feeds, before existing full KEMAR HRIR convolution. The center-band is filtered twice to suppress high-frequency phase leakage, and a further modest +0.085 centered presence band is added to the *already HRIR-positioned direct ears*, never a dry mono voice. The original stereo feeds remain authoritative for decoder observation, P9.2 world bass and room reflections. No dry/headlocked vocal injection or extra FIR. This can enhance centered vocal *and centered instruments*; it is NOT an AI stem separator.
3. **Early-reflection air:** +0.24 of the high-pass complement above 4.5kHz, applied only to the existing two-ear directional early-reflection result. No inter-channel mixing, Haas timing, repeated reverb worker or extra spectral EQ on the diffuse tail. This aims to improve high-frequency spatial cues without simply making room reverb hissier.

A small +0.038 high-frequency direct-air complement preserves the already-external KEMAR cymbal/treble energy when the center-band is boosted, avoiding the earlier P9.7 first-pass regression where 7.5kHz electrical energy fell by ~13% at 10m. An explicit no-high-loss test now rejects that outcome.

The three gains ramp together over 45ms as distance moves through 2–6m. All bands use control-side prepared coefficients, bounded stable filters, fixed memory and no callback allocations. No new EQ slots, public schema, selectable render modes, fake stereo widening or additional HRTF graph. P9.5 output makeup and 0.98 linked peak guard remain unchanged.

## Gates before approving listening audio

1. Independently compile 44.1/48k source tests for exact ROOM OFF and <=2m, voice/air/bass response, finite PCM, antiphase symmetry, bounded room-modal amplification and no device-restart stale memory.
2. Compare complete P9.6 and P9.7 native PCM *hashes* at 2m and ROOM OFF, and target bands at 3.1m and 10m (front and back) for 44.1/48k, using the very same source fixture. The checkpoint must remain bit-for-bit identical in disabled scenes. This guards known-good sound rather than relying on nominal gains.
3. Compile exact Windows JUCE application, V11 state/ACK, sample-peak safety CTests, UI Chromium and source-provenance portable in a branch-qualified build. No public release/merge based on a green Linux source test alone.
4. Operator headphone A/B at matching LUFS and also matching *GAIN setting*: kick/tom attack, bass audibility and decay, center vocal location and intelligibility, edge/cymbal air, interaural position, stage distance, reverb body, 0/90/180/270° and 44.1/48k. Reject nasal vocals, metallic cymbals, mono collapse, bass masking, over-limiter pumping, apparent image moving inside the head. User preference controls promotion.
5. No marketing claim of source-separated vocals, customized individual BRIR, measured live concert loudspeaker impulse response, true-peak certification or dearVR acoustic parity.

**Checkpoint:** P9.6 remains branch/PR #727 and this draft P9.7 is entirely removable by deleting its child branch. Feature OFF recovers P9.6 binary behavior and no new user parameter exists.
