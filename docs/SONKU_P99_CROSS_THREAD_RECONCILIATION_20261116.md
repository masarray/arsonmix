# SonKu P9.9 — cross-thread reconciliation and integration contract (2026-11-16)

This is a deliberately **non-merging** reconciliation of independently active developer threads. This branch is a draft, not public release/main, and this document **does not authorize a force push, PR retarget, cherry-pick of entire files, auto-merge or rewrite of other threads**.

## Live GitHub topology checked

| Work | PR | Head at audit | Base / status | Ownership |
| --- | --- | --- | --- | --- |
| Approved Venue P9.7 | #728 | `fda0eef669e0a59575c489e149662c1bf878c998` | P9.6 draft | Immutable approved audio checkpoint |
| Signature/Cinema/Studio P9.8 | #729 | `4a9e5b2c455818c7a0dc8507ec2b7863be215033` | P9.7 draft | SonKu typed scenes 0–3; original v12 |
| Orbit default / warm vocal-room P9.9 | #730 | `94255ba476d1896cbd54904a87f2b9927851a360` before this reconciliation | P9.8 draft; native Windows 13/13 green | SonKu native audio and GUI |
| ImmerSIB concert / room cohesion | #732 | `c32345f9f638dfb837c60ec84405a5ef315631ac` | Separate ImmerSIB #722 branch; draft; GitHub reported mergeable=false | Product-owned static scene, private voicings 4/5 |
| ImmerSIB single engine | #722 | `b693618cabe9c13e8756633c1d64607b9254cde6` | P9.1/P9.2 draft | Separate product, ONE shared native SonKu PCM core |
| Native floating FX ownership | #731 | `61d64cd76ca56d51a8dcd4d621eea49ac4ca7819` | main, draft | Owner of `NativeFxWin32Owner.h`, `NativeOwnedFloatingWindow.h`, editor lifecycle |
| Factory reset | #708 | `a97462c2730f3e0feae1b9e8982c2784e81ea9a6` | main, draft, non-mergeable at audit | Native session / mixer recovery |
| Guitar FX redesign | #668 | `0a4209b06352f6cc6c0aca27ce99f3615eacd6f5` | main draft | JUCE guitar visuals, independent acceptance |

Main was `37985f02caecd93b22f853183888d81e18dc6736` at audit. `main` is an ancestor of SonKu P9.9, but P9.9 includes **353 commits, 166 file changes compared to main**, most from older stacked research stages. Applying the entire P9.9 branch as a replacement for main or rebasing/merging other branches blindly is **not** a responsible integration. ImmerSIB #732 is divergent from P9.9, with independently developed SonKu voicing/header changes. Do not discard either.

## Confirmed API and sonic conflicts

1. On P9.9 the base class `SonKuSpatialProcessor` was mistakenly `final`, while ImmerSIB's `ImerSipP92Processor` derives from it. **Corrected only P9.9**: base now has a product-specific protected initial-scene constructor, queued-scene admission hook, and unpublished-state adoption with control-side validation and native ACK preservation. The derived product and its identity/state/editor are not copied into the SonKu branch.
2. The P9.9 `SonKuSpatialCore` originally activated its vocalist center/330Hz room stage merely because the project-wide compile flag was ON and a scene was ID 1/2/3. **Corrected**: a branded product sharing the same core is neutral by default; only the **8D-SonKu** native insert explicitly opts in *per instance* before prepare. This is not a second renderer, a new persistent state field, or a listener guess. Other products keep their scene DSP unmodified even in the same compiled application.
3. ImmerSIB #732 has native scene IDs **4/5** and different `SonKuP98SceneVoicing.h` / `SonKuSpatialCore.h` files, while SonKu #730 only has neutral/Signature/Studio/Cinema **0–3**. These remain separate until an explicitly reviewed additive integration preserves **all** IDs and the two product contracts. Do **not** raise SonKu ID validation to 5 without the actual ImmerSIB implementation/tests.
4. FX editor #731 changes `CMakeLists.txt`, `src/Main.cpp`, and native window ownership. Those ownership changes must be replayed with a real three-way merge when integrating, **not** overwritten by the SonKu versions of those shared files. The two native window headers in #732 currently match #731 blob SHA, but future updates must be inspected again.
5. P9.9's **ORBIT STAGE · DEFAULT** (127°, 12s, 2.7m, 100% ER, 77% late, +5.1dB) remains for **new SonKu instances only**. Existing .smix sessions, ImmerSIB's factory, legacy v11/v12 restore, and the P9.7 10m Venue are untouched.

## Objective gates added before any integration

- SonKu P9.9 actual private DSP remains **opted in** in its real native processor, the full integrated scene test and factory Orbit source test.
- A second copy of the **same full KEMAR + directional six-wall + late-room + limiter** test is built with P9.9 compile flag **ON but per-instance opt-in OFF**. The gate compares every complete output row and PCM hash to P9.8 native reference at **44.1/48k, front/rear, all six scenes = 24 cases**, with **zero tolerance**: `scripts/verify_sonku_p99_shared_core_isolation.py`. This is the shared-engine protection for ImmerSIB/other products, not a cosmetic capability check.
- Native JUCE test actually instantiates a branded derived processor, verifies own initial static scene, rejects forbidden moving scenes, checks applied edit ACK/readback, allows control-side unpublished state adoption, and rejects publication-time overwrite. Existing 13 Windows CTests remain active.
- Venue scene=0/ROOM OFF/MIX0 continue their exact P9.8 PCM hash gates, including all independent P9.7 approval fixtures.
- No additional audio latency, heap allocation in the PCM callback, extra HRTF, or duplicated convolution pipeline introduced.

## Safe future integration order (not executed here)

1. Keep separate reviewed source checkpoints. Do not merge P9.9 in isolation from its P9.5/P9.6/P9.7/P9.8 ancestry. The release decision needs owner headphone/ASIO A/B approval and a single explicit coherent integration plan.
2. Integrate **#731** native floating editor lifetime/ownership by three-way code merge of `src/Main.cpp` and `CMakeLists.txt`, preserving its tested ownership and source-specific closing/reopen behavior; avoid overwriting from old SonKu snapshot.
3. Reconcile **#722/#732** independently as ImmerSIB's product-owned branch. Forward-port the typed voicing IDs 4/5, IMRS v7 restore constraints, authorized static-scene constructor, audio-gate and GUI assets deliberately; verify no loss of SonKu IDs 0–3, no new PCM authority, and no accidental P9.9 change to ImmerSIB presets.
4. Only after integrated Windows JUCE/Chromium/PCM and product-specific session regressions pass, and owner explicitly approves acoustic candidate, decide main release. Keep draft PRs and their SHAs as reviewable provenance.

**This reconciliation intentionally changes only P9.9 branch-owned native API compatibility and opt-in guards.** No writes to main, #731, #732, #722, #708, Guitar FX, checkpoint Venue, or shipped installer. Any cross-product *compile* with combined #732 is not yet proven because those PR branches are divergent and their scene IDs/headers still require a manual three-way integration.
