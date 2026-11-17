# Milestone D5 — corrected ImmerSIB/SonKu Windows FAT (18 November 2026)

## Why this candidate exists
The previous FAT candidate [PR #756](https://github.com/masarray/arsonmix/pull/756) / [run #44081695177](https://github.com/masarray/arsonmix/actions/runs/44081695177) compiled the mixer from generic CMake defaults. Although the audio engine played and 100/100 CTests passed, the ImmerSIB C++ catalog descriptor was compiled **out** (default `ARMIX_IMMERSIB_RELEASE_CANDIDATE=OFF`). The EXE contained web-facing `ImmerSIB` text but not the native `NATIVE_IMERSIP_PREPARE_EXCEPTION` marker. Do not reuse that build.

[Corrective PR #759](https://github.com/masarray/arsonmix/pull/759) merged as `85d84195d06c8798ce3a4309b362766c13b07e14`:
- FAT/field configure both officially shipped spatial switches ON, without globally modifying research defaults.
- Packaging and independent distributor verification reject mismatched `CMakeCache.txt`, missing native catalog literals, or any missing byte-exact binary scene resource.
- The existing approved ImmerSIB/8D-SonKu P9.9 DSP, presets, editor graphics, IMRS state, .smix, routing and ASIO callback remain unchanged.

## Software acceptance / provenance
1. Exact-source Windows FAT job on this PR SHA; no branch YAML trust bypass.
2. Confirm `WINDOWS_FAT_SPATIAL_CONFIG_VERIFIED`, `WINDOWS_SPATIAL_PRODUCT_FAT_PASS` in **both** package and distribution verifier steps.
3. CTest must include `NativeImmerSIBJucePresetACKStateAndBypass` and `NativeImmerSIBP99VocalRoomAndVenueGolden`, alongside all prior 100 native tests.
4. `BUILD_PROVENANCE.json` must include `spatialFxBuild=SONKU_P99_AND_IMMERSIB_NATIVE_CATALOG_PLUS_SIX_EMBEDDED_SCENES_VERIFIED` and `spatialFxCatalogId=armix.builtin.imersip.v1`; preserve recorded exact SHA and EXE SHA256.
5. Portable and Installer ZIP upload; first-install, running-app graceful upgrade, genuine prior release upgrade/rollback verification.
6. Public alpha.26 remains unchanged. New FAT artifact is not automatically a published release.

## Operator visual/audio acceptance on Windows (P0)
- Open the corrected portable app with a disposable or backed-up .smix session. In Channel FX Picker, find **ImmerSIB** under Spatial as well as **8D-SonKu**. Do not count a static label as a loaded processor.
- Insert ImmerSIB into a real native ChannelId and open its native JUCE editor; all **seven concert venue scenes** and Power/bypass must be functional, including Concert Front Row XL.
- Confirm existing .smix IMRS insert ID `armix.builtin.imersip.v1` survives Save/Open, native ACK/readback, FX rack OFF/ON, editor close/reopen and device restart. Keep mixer MONITOR at a safe level.
- Compare approved ImmerSIB concert preset sound to the already accepted alpha.26; no voicing, gain, bass, scene or movement retune is authorized in this fix.
- Re-test real ASIO/WASAPI IN → CH05 → MAIN; SOURCE ONLY retains input meter but silences MAIN; BUS and SEND PRE/POST/ON/OFF, rack bypass, physical output mute, and no stale PCM following device changes.
- Mark hardware FAT PENDING on any untested backend/device; report exact device, sample rate, buffer, output pins, and Windows diagnostic marker if a dropout occurs.

## Safety boundaries
Do not automatically replace the stable published alpha.26 or force-merge unrelated historical SonKu/ImmerSIB PRs. Do not close #412/#547/#15 without physical evidence, and do not claim GitHub ruleset enforcement until the required status context has actually been configured.
