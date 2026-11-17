# ImmerSIB private P9.7 Venue + P9.8 native engine port (16 Nov 2026)

**Scope:** experimental ImmerSIB PR #722 only; production main stays unchanged, old IMRS sessions, stable `armix.builtin.imersip.v1`, and saved .smix identities remain. Preset labels displayed as **ImmerSIB**. GUI art work is independent and not declared visually accepted.

## Sonic reference
The **owner-approved 8D-SonKu P9.7 Venue Mega Space** is the golden reference, branch `checkpoint/sonku-p97-venue-mega-space-owner-approved-20261116`. P9.8 branch `feat/sonku-p98-signature-cinema-studio-voicing-20261116` includes every P9.4–P9.7 stage contribution, and original P9.8 ear/world-source voicings. The actual renderer remains **one native SonKuSpatialCore, mode 9** via `SonKuSpatialProcessor` and existing SPSC/ACK, not a parallel ImmerSIB DSP.

**Exact Live Concert Venue world scene:** `moving=false, startAzimuth=0deg, stage=106deg, Large 28.8m, distance=10.0m, Early=100%, Late=100%, MIX=100%, outputTrim=1.8 linear, sceneVoicing=0`. This is the same `VENUE` scene defined by `tests/sonku_p98_integrated_scene_test.cpp`. Using voicing=0 deliberately turns OFF P9.8 extra source/early/ear bands so Venue stays the unaltered P9.7 golden. Tests compare full PCM output hash at 44.1/48k to the explicitly recreated reference.

Other operator-facing scenes: Royal Concert Hall, Arena Thunder, Front Row Power, Symphonic Palace (P9.8 Signature voicing 1 with differing physical stage/room distances), Velvet Cinema (P9.8 Cinema voicing 3). Those are **audition candidates**, not perceptually approved. There is no actual music stem extraction, content-aware separator, or source classifier. P9.8 center band is a modest bilateral source emphasis fed into measured world HRIR before ears; a realistic subjective claim of independent instrument stems would be false. Premium quality requires matched-loudness user A/B.

## Engineering
- P9.8 source forwards acoustic modules P9.4 front/back, P9.5 music presence, P9.6 reinforcement, P9.7 stage impact, and P9.8 typed voicing, together with P9.8 core and exact P9.2 foundation. Each imported file is fingerprinted in `config/spatial_audio_authority.json`.
- Native SonKu transport is P9.8 v12 but retains ImmerSIB's restricted derived scene admission, the existing virtual methods, and legacy product ID. The SonKu **default standalone constructor retains its original neutral initial scene** in this branch, avoiding unrelated automatic preset changes.
- The user-approved Venue is not modified by P9.8 voicing: `sceneVoicing=0`. Scene changes use existing native SPSC command/ACK. GUI maintains typed voicing with a single accepted scene.
- An enabled P9.8 private build writes append-only **IMRS v7 = 8-byte IMRS header + 67-byte native SONK v12 = 75 bytes**; strict preflight protects against partial state mutation. Previous **IMRS v6 + SONK v11 (71 bytes)** and older IMRS v1–v5 formats restore with neutral voicing. Existing .smix audio slots use the same identity.
- Private `ARMIX_SONKU_EXPERIMENTAL_P94_...`, `P95`, `P96`, `P97`, `P98` CMake flags stay OFF by default. The dedicated ImmerSIB CI config explicitly enables the full five-stage stack; the standalone PCM check compiles exact equivalent flags to prevent a false-green different renderer.
- The editor supports 10m Venue instead of clipping the value to the previous 7.4m UI limit; scene renderer retains safe speaker geometry.

## Acceptance boundary
1. Fast source test: exact Venue golden PCM hash, all scene profiles have actual distinct non-NaN output, anti-phase stereo doesn't catastrophically null, linked guard sample peaks <= 0.9805 at 44.1/48k.
2. Windows native JUCE: exact per-slot FX behavior, typed scene presets, native ACK/readback, IMRS v7/v6 and v1-v5 compatibility, editor actual layout and package verifier.
3. Real user headphone acceptance: kick/tom attack, warm/controlled LF resonance, centered vocal intelligibility, preserved cymbal/treble brightness, instrument clarity, front/rear depth and immersive stage. Loudness-match carefully; stronger gain alone is not proof of detail or premium quality. The owner-approved Venue must remain selectable and bit-exact.
4. PR remains Draft; no release/merge until owner validates.

**Notes:** old `tests/imersip_p92_factory_music_test.cpp` remains the historical P9.2 data fixture built without P9.8 flags. New `tests/immersib_p98_native_sonic_gate.cpp` tests the actual P9.8 world engine. Neither silently re-qualifies P8.3 historical rejected mode.
