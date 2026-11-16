# ImmerSIB Concert Front Row XL + vocal/room cohesion candidate (2026-11-16)

## Source of truth and UX

Owner's native JUCE screenshot shows a **Custom** scene with: LARGE 28.8 M, Stage Width **147°**, Source Distance **1.0 m**, Early Room **180%**, Late Room **60%**, Spatial Mix **100%**, Ear Align **0°**, Head Yaw **0°**, Master Gain **+2.5 dB**.

These visible controls are added as typed preset **ID 8, Concert Front Row XL** (scene voicing **4 / Detail Warm**). An important limitation: the screenshot *does not display the hidden serialized scene voicing*. Choosing ID4 is a documented assumption, not proof of bit-identical sound to the actual saved Custom session. The original .smix / IMRS settings remain authoritative; no session format change and no migration of UI preset IDs. All six prior presets retain their values.

No new GUI image was synthesized. ID8 reuses the existing Live Concert background because the screenshot visually shows the live venue. GUI artwork, layout and dimensions remain owned by the GUI thread.

## Root-cause hypotheses, not promises

1. P9.8 center-source body/presence is processed before the world-positioned KEMAR HRIRs, but the six-surface early reflections and diffuse late-room processors previously consumed unchanged stereo input. At high Early Room this can make direct spectral body and room spectral body perceptually inconsistent.
2. P9.5's upward-only adaptive makeup has a 1.65 maximum, followed by P9.7/P9.8 spectral makeup and an existing zero-lookahead 0.98 peak limiter with 80ms release. A strong mastered source can therefore modulate the entire output, even when the tone changes alone sound promising.
3. An independent true-vocal stem is unavailable: L+R center is NOT exclusively the singer, and L-R SIDE is not only instruments. Static spectral notches are not stem separation.

Research motivating the experimental candidates: Arweiler & Buchholz, JASA 2011, DOI 10.1121/1.3609258 (spectral characteristics of early reflections); Bradley et al., JASA 2003, DOI 10.1121/1.1570439 (early reflection contribution to intelligibility); Prodi et al., JASA 2022, DOI 10.1121/10.0011403 (spatial perception and early/late balance). None of these papers validates arbitrary numerical mix coefficients for Dua Lipa concert material.

## Narrow candidate changes

- **Only mode 9, room enabled, and sceneVoicing 4/5**: reuse a modest fraction of the existing, pre-HRTF warm source delta in the genuine six-wall early-reflection feed (32%) and original late-reverb feed (12%). The room maintains its original direction/tap/geometry/late decay; no extra HRTF, delay, crossfeed, AI or vocal stem.
- **Only same warm scenes**: P9.5 upward makeup maximum **1.10× (~+0.83 dB)**, rather than 1.65×. The 280ms detector / 450ms slew is preserved and smooths switches. Previously accepted voicing 0–3 and golden P9.7 remain on the unchanged 1.65 path.
- **Existing linked peak guard retained**. An exact preset with +2.5dB does not guarantee no limiting on mastered peak-heavy music. Do not promise no pumping until real PCM gain-reduction and matched-loudness user FAT are done.
- **Offline diagnostics compile flag**: `SONKU_IMMERSIB_PCM_DIAGNOSTICS=1` for the ImmerSIB native acoustic gate only. Counts limiter-hit frames and sums makeup/gain reduction; not available in production callbacks. Do not read counters concurrently with live audio.

## Required qualification

- Native C++ at 44.1/48 kHz including all seven scenes, owner screenshot exact controls, original P9.7 Venue PCM hash, mono/anti-phase/finite/peak, adaptive makeup bound, original legacy voicing parity.
- Actual native JUCE parameter dispatch/readback, IMRS7/SONK12 session restore, preset dropdown and Windows build/portable artifact.
- A/B at **matched LUFS** on the exact permitted concert tracks: new ID8, prior custom IMRS, unchanged reference, and previous warm candidate. Track voice body/articulation, phase/coherence, ER high-frequency coloration and kick-triggered pumping. A green CI is not listener approval.

## Status

Development candidate on a branch stacked over draft PR #722. This document deliberately does not claim subjective listening approval, proof of isolated voice detection, calibrated true-peak, or physical-speaker parity. No production merge or release.

## User revision 16 Nov: frontstage distance and reflected high-air

The owner revised the desired distance to **1.1–2.5m**. This overrides
the previous 1.0m screenshot *only for the new listening candidate*;
that earlier screenshot remains recorded above as historical provenance.

- P9.8 product GUI, typed presets, scene validation and IMRS v6/v7 +
  older legacy migration share one 1.1–2.5m envelope, preserving room,
  gain and voicing. Distances now: Live 2.0m, Royal 2.2m, Arena 2.5m,
  Front Row 1.3m, Symphonic 1.9m, Cinema 1.8m, Front Row XL 1.1m.
  Existing 1.0m/10m files load at the closest accepted bound;
  session bytes, catalog IDs and the private P9.7 10m Venue fixture
  remain unchanged.
- Only voicings 4 and 5 add a **small linear (>5.2kHz) lateral term**
  to the existing, already directional KEMAR EARLY-reflection output:
  gains 0.18 (Detail Warm) and 0.14 (Impact Warm) multiply the ear
  difference of the highpassed early component. The early MID, direct
  HRTF, late reverb, reflection wall geometry and delay are unchanged.
  No artificial Haas delay, extra HRTF, singer stem, compressor, FFT,
  parameter ID, or realtime memory allocation is introduced.
- This is **not a blanket HF increase**. Directional early image
  energy can improve stage width and envelope fusion, but overly
  bright/loud reflections increase metallic sibilance or mask vocals.
  Inspect native PCM 7.4kHz lateral/1.5kHz impact and matched-volume
  concert audition before approval. High-frequency side gain is a
  deliberately small *candidate*, not a calibrated room model.

Reference for broadband lateral early reflections and preserved envelope:
Lokki et al., JASA 129 EL223 (2011), doi:10.1121/1.3579145.
No research source proves any single numerical gain optimal for this
mastered concert recording.

**Gate:** both 44.1/48k native tests, old Venue PCM hash, mono symmetry,
native JUCE state migration and source-only/ROOM OFF integrity. Keep
PR Draft, and reject/retune if the owner's headphone FAT hears harshness
or unchanged pumping.
