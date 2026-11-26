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
