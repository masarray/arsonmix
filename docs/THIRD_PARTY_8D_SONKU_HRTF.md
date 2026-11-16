# 8D-SonKu measured HRIR redistribution and attribution
Date 2026-11-12. Source: 8D-SonKu `SonKuMeasuredHrir.h`.
- Original HRIR measurements: Bill Gardner and Keith Martin, MIT Media Laboratory, 1994. MIT KEMAR (0° elevation).
- MIT Media Laboratory terms are reported in the OpenAL Soft MIT_KEMAR.def: free of charge without usage restrictions provided original authors are cited. https://github.com/kcat/openal-soft/blob/master/utils/MIT_KEMAR.def
- C table format and 44.1/48 kHz processed 128/140-tap values derived from **Aristotel Digenis, MIT HRTF C Library**, https://github.com/jhonmark/mit-hrtf-lib (MIT license; copyright 2007 Aristotel Digenis; repository LICENSE copyright 2014 greekgoddj).
- Existing public source path: `source/normal/mit_hrtf_normal_44100.h` and `source/normal/mit_hrtf_normal_48000.h`; only the 0° elevation 0–180° at 5° spacing; 44.1 kHz rows zero-padded to 140; negative 180..360 by ear symmetry.
- **P7 research-only** `engine/builtin_fx/sonku/SonKuMeasuredHrir3D.h` additionally copies the original MIT-licensed measurements at **14 elevations (−40..+90°)** with the exact upstream nonuniform azimuth grids (368 measured angle pairs/sample-rate, including 0° reused from the native public bank). Upstream Git blobs `5e5bcac17b0d59f1b8e7fee04b380170f7d521d2` (44.1k) and `39931add922dad4687e572059bc157b124926944` (48k). The extractor `scripts/generate_sonku_measured_hrir3d.py` regenerates this checked-in bank from locally supplied verified source headers; no runtime/network dataset fetching or proprietary vendor coefficients.
- The extra P7 measured bank compiles only with `SONKU_P7_MEASURED_ELEVATION_RESEARCH=1`. No P7 installer redistribution is authorized yet; retain this MIT attribution and all third-party notices in any future test package.
- **These are general-purpose nonindividualized HRIRs**. Front/rear externalization is NOT guaranteed. No dearVR code or IR is used.

MIT License (MIT)
Copyright (c) 2014 greekgoddj
Copyright (c) 2007 Aristotel Digenis

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Packaging gate: ensure this attribution/license is included in release distribution notices; source-only doc is not enough to certify an installer. Dataset provenance should be independently legal-reviewed before any commercial redistribution claim.
