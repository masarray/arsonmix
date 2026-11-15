# ImerSip: P8.6 horizontal-first room extraction — research gate (15 Nov 2026)

## Why
Owner's physical ImerSip test still unacceptable; audit of GitHub
run 43671493675 revealed **real Windows SegFault**
in ImerSipNativeJuceP82AcceptedStateAckAndRestore. Exact test source had
FOUR large SonKu/ImerSip processors constructed on the same Windows
~1MiB stack; running them in Linux earlier did not prove Win64 safety.
Commit `7d789aa9` now heap-owns all four JUCE test processors; the
Windows CTest and exact portable ZIP must be revalidated before use.

Owner also requested inspecting P8.6, PR #711 commit
`884e1440f2e415f8731113e912d636081db8a9d0`.
P8.6 is a **private**, unapproved, isolated P8.5 stack that targets
floor/ceiling diffuseness without abandoning a directional speaker.
Do not merge P8.6 or its P8.5/P8.3 ancestry onto this ImerSip P8.2 fork.

## Exact conceptual extraction (no new DSP engine)
In the same existing SonKuEarlyReflections six-wall renderer, ImerSip
passes a new optional `imersipHorizontalFocus=true` only when the
ImerSip measured two-speaker feature is active.
It keeps the real six paths, FIR, geometry, materials and delays:
- left, right, front, rear: **1.00x** original pressure;
- floor: **0.24x** original pressure;
- ceiling: **0.15x** original pressure.

These exact coefficients come from P8.6 and affect **only the reflected
path after convolution**. The dry KEMAR binaural HRIR, fused P8.4
HP750 contrast, direct stereo original MID/SIDE, coherent LF120 shelf,
1.6m source and native SPSC/GUI contract stay unchanged. It is a static
front-orchestra stage, NOT rotating or head-tracked audio.

The optional flag defaults to FALSE, preserving normal 8D-SonKu P8.2
output. The research lever changes no preset/session state and creates
no branch-dependent copies of room arrays, delay rings or FIRs. P8.6's
ER presence 0.76 and FDN presence 0.38 are NOT blindly imported because
ImerSip already has separate bounded, measured EARLY/LATE mixing laws,
and the owner's immediate regression was that previous wet returns were
INAUDIBLE. Relative audible comparisons must be measured first.

## Engineering gates
- Original SonKu normal-path bit-exact OFF flag, original bass,
  anti-phase SIDE, 44.1/48k 64/128/8192 chunk parity.
- Source test evaluates per-path weights 1/1/1/1/.24/.15, live six-wall
  room PCM with and without ImerSip focus at both rates, nonzero room
  energy and actual output difference, not just source-code inspection.
- Real JUCE state/ACK/audio callback CTest after heap stack fix,
  Windows EXE+ZIP manifest SHA and provenance.
- Real owner headphone A/B vs approved P8.4 and P8.5/P8.6; focus can
  make sound drier or more head-locked. No claim of premium immersive
  acceptance based on CI alone.

Reference: https://github.com/masarray/arsonmix/pull/711
