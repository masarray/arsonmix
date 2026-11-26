# SonKu P10.1 — Raster fidelity and continuous orbit (2026-11-26)
 
Owner requested a smooth raster listener, not a synthetic SVG person, and explicitly approved the existing non-colliding orbit. This change is presentation-only. Preserve STAGE (200,174,148,83), inverse projection, two speaker nodes, near/far painter ordering, separation/tethers, native bridge, presets, and all acoustic source blobs. No new runtime dependency.
 
## Asset
Existing rear-view PNG blob ea3949f1419c51c5d297b21efe78489b075471f2 had 79 palette entries. Restored from that exact raster: alpha-aware 5-tap separable binomial smoothing of color (88% smoothed, 12% original), retain original alpha at 256px, 2x premultiplied bilinear resample, 512x512 8-bit RGBA PNG, Paeth scanlines, lossless DEFLATE. 201533 bytes, 19417 distinct RGB values among nearly opaque pixels. This is restoration, not recovery of missing original detail or a new 3D render. The same source silhouette and placement are retained.
 
## Rail and speaker contract
One complete ellipse with continuous user-space depth gradient. The same ellipse is reused only where the near rail would otherwise be hidden by the listener alpha silhouette. No separately authored arcs, mismatched dash patterns or horizon join. Existing speaker painter planes retained to preserve occlusion; same shadow in both planes, continuous scale/opacity, restore keyboard focus if reparented. Pointer capture stays on the stable map. No camera/orbit coordinate change.
 
References: W3C PNG 3 https://www.w3.org/TR/png-3/ ; MDN gradientUnits https://developer.mozilla.org/en-US/docs/Web/SVG/Reference/Attribute/gradientUnits ; MDN animation performance https://developer.mozilla.org/en-US/docs/Web/Performance/Guides/Animation_performance_and_frame_rate .
 
## Gates
- Existing assembly check pins true-color asset Git blob and size.
- Existing geometry contract checks full sweep across 360 phases and spreads 0..180 by 5 degrees, near-plane speaker shell vs conservative cranial envelope; far occlusion is deliberate.
- Compiled browser smoke checks one ellipse, masked reference, asset dimensions, painter ordering, power state, drag and native ACK; inspect actual screenshots at both horizons, front/rear, and zero spread on Windows before visual acceptance.
- No DSP source changes. Externalization and FIR optimization remain separate from this visual repair; preserve owner-approved P9.9 and shared ImmerSIB audio. See #664 for separate level-matched acoustic qualification and native hardware profiling. No release authorization implied.

## P10.2 approved raster and premium compact finish (2026-11-27)

The owner rejected the initial smoothed indexed raster and subsequently approved the independently rendered blue satin listener candidate. Exact approved PNG: 512×512 RGBA color type 6, 177803 bytes, Git blob SHA-1 31de77d1126d318d81ff43297d97adb2187bccfa (SHA-256 86e421c0af66e061f42bbc6077d2f7131b3d44269d527c5d09770d6236bcaefe). Reconstructed from the delivered Git binary patch and verified by Git blob identity, with no further re-encoding. Prior silhouette analysis reported zero near-plane cranial intersections among 65,159 near-plane poses (130,320 total positions). The first restoration is superseded.

Existing FX Rack CSS owner refined in place: graphite satin surfaces, restrained cyan accents, compact readable labels, precision knobs, reduced glow, neutral wait LED, visible focus and disabled affordances. Preserve 790px window envelope, exact orbit and image placement, speaker geometry, native bridge and presets. Pointer inverse projection uses SVG getScreenCTM for borders and letterboxing. Browser smoke adds responsive viewport geometry checks at 680/390px. No additional dependencies or DSP edits. Exact-head CI and actual visual review still required.

## P10.2 CI recovery and visual scheduler

Run 45881328210 failed at the new 680px responsive assertion: native editor was 790px wide (left 8, right 798), zero internal overflow and zero SVG transform error. This is the intentional FUX-1 fixed hardware clip/pan contract, not an audio or PNG fault. Scope SonKu narrow media queries to non-native browser; leave the shared clip owner in charge of native positioning. Native regression checks retain width, stage and SPACE geometry and verify actual close reachability after pan at 680 and 390; separate no-transport browser instance checks responsive fit and internal overflow. No relaxed assertions or skipped tests. Visual animation now uses one conditional frame owner; no continuous RAF scheduling while static, bypassed, dragging, closed or document hidden, and resume resets the visual time origin without changing audio clock. Deterministic scheduler test covers idempotence, cancellation and no catch-up phase jump. No DSP/preset/asset modification. Exact-head Chromium CI is still required.

## P10.3/4 — parity-first DSP efficiency candidate

P10.2 Chromium run 45882581965 completed successfully. Two source-level optimizations: direct FIR ring traversal split into two contiguous spans (preserve ascending tap accumulation and per-sample coefficient slew), and three fixed raw HRIR caches for stationary sources. Cache is invalidated on reset, successful settings transaction and offline elevation edit; moving sources use unchanged interpolation. P90 coherent MID, stereo SIDE, room retarget, 16-sample update cadence and limiter remain unchanged. No allocation, latency, parameter or preset change; approximately 3.4 KiB extra per-instance fixed storage. Existing asset and GUI unchanged.

Qualification script compiles pinned pre-P10 core and candidate with the same compiler/flags, for legacy and full P9.9/stage-monitor configuration. Compares every float bit, 44.1/48 kHz, static/moving, spread 0/127/180, XYZ/yaw, room transitions, dry/wet, mixed block sizes, reset and re-prepare. Same-runner two-order timings are informational only. No externalization improvement claimed: acoustic enhancements require separately level-matched listening evidence. Do not merge until exact-head CI and appropriate Windows/ASIO validation; no change to approval gates.

### Isolation decision

Candidate source changed the locked SonKuSpatialCore.h blob, triggering both approved ImmerSIB provenance gates on run 45885156174/45885156107. This is expected source identity rejection, not measured PCM drift. Do not re-seal the owner approval. Keep GUI PR #951 independently integrable; branch the DSP candidate separately until exact bit parity is demonstrated, then follow the existing explicit provisional implementation admission procedure. Extended parity driver uses the exact ImmerSIB factory scene helper and all three production per-instance opt-ins, covering seven approved scenes and dry/room-off at both rates. Candidate has not been measured yet.
