# 8D-SonKu — Technical & Perceptual Research Evidence
Date: 2026-11-12. Status: desktop research, not first-hand FFT/CPU/ASIO/listener evidence. Scope: lawful independent internal FX, not copying dearVR code/IRs.

## Sources and what they do / do not prove
1. **dearVR PRO 2 official manual (Jan 2024)**: https://files.plugin-alliance.com/products/dearvr-pro-2/dearvr-pro-2_manual.pdf
   - Chapters 1–3 identify HRTF binaural directions, distance, coordinate input, early reflections with source position, late reverberation and 46 virtual acoustic presets. Product also admits front/back confusion and importance of head motion.
   - **Implication**: reference UX and acoustic *requirements*, not a license or recipe to replicate proprietary algorithms, graphics or impulse responses.
2. **Steam Audio SDK C API guide and HRTF**: https://valvesoftware.github.io/steam-audio/doc/capi/guide.html ; https://valvesoftware.github.io/steam-audio/doc/capi/hrtf.html ; https://github.com/ValveSoftware/steam-audio
   - HRTF is a direction-dependent pair of L/R filters. Bilinear directional interpolation is smoother than nearest (at higher cost). Expensive HRTF construction must happen outside audio thread. Reflections can be much heavier. Open source Apache-2.0 code, source distributions/NOTICE obligations to review.
   - **Implication**: engineering reference and possible tested backend; NOT proof its runtime is allocation-free under ArSonMix callback or compatible with every supported small ASIO buffer.
3. **SOFA / AES69**: https://www.sofacoustics.org/ ; https://www.sofacoustics.org/mediawiki/index.php/SimpleFreeFieldHRIR
   - Standard interoperability format for HRTF and spatial room IR; orientation, coordinates and receiver ordering are defined by convention.
   - **Implication**: establish data contract, reject unsupported data and explicitly check dataset redistribution license before bundling.
4. **libmysofa**: https://github.com/hoene/libmysofa
   - Provides C SOFA file reading/resampling/interpolation tools, code under BSD-3-Clause (with third-party/component and dataset license review).
   - **Implication**: useful OFF-RT dataset reader, not a complete optimized spatial/room renderer.
5. **Auditory localization practical review (2024)**: https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2024.1408073/full
   - Generic/non-individualized HRTFs commonly produce front/back confusion; reverberation sometimes improves localization but findings vary.
   - **Implication**: cannot market front/back realism as universal or solve it solely by adding more reverb.
6. **Frontal binaural externalization study (Aug 2026)**: https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/
   - Reports improvements in externalization via level and strategic early-reflection adjustment of non-individualized binaural room IRs.
   - **Implication**: source-dependent early reflections/direct-to-reverb relationship should be evaluated with blinded listening, not only acoustic numerics.
7. **2025 room-based binaural conference study**: https://pub.dega-akustik.de/DAS-DAGA_2025/files/upload/paper/397.pdf
   - Discusses plausible binaural room rendering from direct sound, limited early reflections and common late reverb. Full ray tracing is not an initial requirement.
   - **Implication**: prototype a bounded hybrid acoustic room without complex real-time geometry ray tracing.

## Evidence weighting / developer decision
- HIGH confidence: HRTF binaural left/right processing is needed for positional cues; moving HRTF should use smooth interpolation; sample-domain motion must be audio-clock-based; real acoustic reflections matter to externalization; native callback ownership follows one existing JUCE graph.
- MEDIUM confidence: two short-HRIR emitter paths + bounded early reflections + one FDN can achieve desirable CPU efficiency; requires prototype benchmarks and psychoacoustic evaluation.
- NOT ESTABLISHED: 8D-SonKu front-back headphone discrimination quality, CPU and latency on user's rig, shipped dataset license, relative quality vs dearVR, HRTF profile that fits all listeners.

## Early technical trade-offs to measure
| Candidate | Why consider | Risk / experiment |
| --- | --- | --- |
| Steam Audio CPU HRTF backend | Mature direction-dependent engine, documented interpolation, supported SOFA | Native dependency packaging, callback behavior at 64/128 frames, cost with 2 emitters; benchmark and license inventory. |
| Native FIR/FFT backend + libmysofa control loader | Precise fixed-size RT contract, full ownership, little per-plugin binary weight | Needs rigorously tested orientation/delay/interpolation, spectral coloration and front/back listening. |
| Full room ray tracing | Rich physical scene potential | Not justified for static compact room presets; CPU, geometry tooling and render cost excessive for initial internal FX. |
| First-order reflections + FDN | Bounded CPU and sufficient initial room plausibility candidate | Need physically credible delay/direction/gain and real externalization verification. |

## Required benchmark / audio lab protocol
- Fix auditable sample files: impulses/noise/voice/music stem, mono and stereo, seeded deterministic motion; record reference input/processing gain, profile and filter lengths.
- Run same 4-direction and rotating trajectory through both candidate HRTF renderers; compare transfer IRs, ITD, spectral errors, CPU p99, quality at buffer 64/128/256, at 44.1/48/96 kHz. Verify callbacks don't allocate.
- Choose one initially verified, **individually redistributable** HRTF dataset; document version, listener/ear orientation, source elevation coverage, SHA256, license/attribution.
- Blind headphone listening test includes multiple HRTF profiles and several headphone/listener combinations, not just developer's own ears. Record front/rear confusion, externalization, distance, timbral coloration, clicks and preference; compare with normal stereo pan as baseline.
- Avoid misleading use of the term "8D": technical output remains **2-channel binaural** with a dynamic 3D sound field, not 8 physical audio channels.

## Repo-specific verified orientation
- `src/NativeBuiltInFx.h` registers built-in processors independently: Parametric EQ, Compressor, Harmonice, Vocal Throw.
- `engine/core/OrbitMotion.h` is vendor-VST normalized orbit geometry, **not** the native HRIR spatializer and **not** physical-meter mapping.
- `engine/core/LiveChannelFxGraph.h` reuses processor instances across mixer graph revisions and does not require duplicating a JUCE callback.
- `docs/architecture/FEATURE_EXTENSION_PLAYBOOK.md` describes narrow built-in FX path, typed control, verified native insert, session state, bounded CI.
- No `8D-SonKu` implementation was found in default-branch repository code search at the above audit checkpoint; this is not a guarantee that future PRs won't add it.

## Design recommendation
R0: publish PRD and ARD (this branch). M1: implement true 4-direction native HRTF core with one stable insert identity, PCM tests and safe bypass. M2: add sample-driven orbit. M3: real room. M4: premium path editor, calibration. Gate perfection claims on measured listening evidence.

## UI references audited 2026-11-13 (form principles only)
- [Waves Nx Virtual Mix Room user guide](https://assets.wavescdn.com/pdf/plugins/nx.pdf): a human-head-centric visual with speaker icons makes orientation and left/right scene intent recognizable. Adapt **human + monitors**, not Waves' copyrighted artwork or skin.
- [Sound Particles Energy Panner 2](https://soundparticles.com/products/energypanner/overview/): the moving-source visualization is the main interaction canvas; source paths, controls and stereo distribution are visually connected. Adapt **one central spatial display** and visible source markers.
- [dearVR PRO 2](https://spaes.org/dearVR-PRO-2): direct graphical XYZ positioning illustrates why direction should be interactive rather than a hidden numerical parameter. 8D-SonKu M1 stays horizontal 0° elevation; do not advertise full XYZ elevation or personalized distance room simulation absent DSP.

The adapted user experience is **not** based on reverse-engineering any proprietary plugin GUI. Use the already-owner-approved blue bust asset, original SVG perspective ellipse, original mini studio-monitor vector controls and native JUCE/HTML range inputs. Preserve acoustic coordinate signs and distinguish display separation from true source azimuth. See the M4 contract in the ARD and PRD.
