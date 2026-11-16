# ImerSip P9.1/P9.2 — single-engine adoption (16 November 2026)

**User reference:** 8D-SonKu P9.1–P9.2 is reported to sound external and alive on the user's real headphones. Do not replace its acoustic renderer with the older ImerSip P8.2/P8.4 bespoke dry-MID/room-enhancer path.

**New insert audition default** (after owner headphone feedback): `Live Concert`, 150°/2.2 m/Large/Early 100%/Late 91%/+4.8 dB. The original 82°/1.6 m/78%/33%/0 dB reference stays available as `P9.2 Neutral A/B`. Screenshot-match 150°/1.6 m/100%/91%/+4.1 dB is a separate `Your 150° Scene` preset. This is preset-only product tuning; **not** a change to the P9.2 DSP, and neither enhanced gain nor acoustic realism is guaranteed for full-scale mastered material.

**Canonical ownership and CI classification:** [SPATIAL_AUDIO_AUTHORITY.md](SPATIAL_AUDIO_AUTHORITY.md) and [machine-readable ledger](../config/spatial_audio_authority.json). The ImerSip factory scene is **not** the operator's louder/roomier #720 headphone-test scene.

**Baseline:** PR #719 commit `763706a15a2617a10d23b5e2e43eb5689e454ccb`. This integration is a new research branch based on that exact PR, not a merge of divergent #706. Production `main` and both older PRs remain untouched.

## Design

- **One PCM authority:** `ImerSipP92Processor` inherits the existing native `SonKuSpatialProcessor`, whose single callback invokes the exact P9.1/P9.2 `SonKuSpatialCore` renderer in mode 9. This is inheritance of transport/control, not another sound engine. No P8-only `ImerSipDsp`, HRTF staging, secondary limiter, headphone SIDE widening, EQ, early-reflection boost, or LF shelf.
- **Product identity:** same ImerSip native catalog ID `armix.builtin.imersip.v1`, its own native editor and IMRS state, existing host FX slot routing/bypass. P9 CMake flag is private/opt-in, so public builds are unaffected.
- **Factory vs reference:** the first ImerSip insert now starts in `Live Concert` and the **P9.2 neutral A/B reference** remains separately selectable. Original SonKu product defaults and restored user sessions remain unchanged. This separation is enforced by source and native JUCE tests.
- **Real controls only:** speaker angle, source distance (bounded by real room geometry), EARLY ROOM, LATE ROOM, spatial mix, output gain, manual yaw, KEMAR ear alignment, preset and native per-slot power/close. UI edits go through existing audio ACK; no optimistic DSP success.
- **Session:** new `IMRS` v6 embeds the accepted native 63-byte `SONK` v11 state. Old IMRS v1–v5 are parsed and deliberately migrated into P9 **without** re-enabling P8-only Bass Impact/Detail/Stereo Keep DSP. These old three controls have no bit-perfect P9 analogue and cannot be claimed lossless. Existing room/distance maps are validated and clamped to physical geometry rather than rejected or rendering stale filters.
- **Research limits:** P9 bass modes are parametric, not a measured commercial BRIR. A byte-level native PCM comparison proves no new audio processing; headphone perception remains a user-listening qualification.

## Mandatory quality gates

1. Native JUCE P9 core parity: same PCM sample-for-sample as ImerSip insert at 44.1 and 48 kHz with actual stereo music fixture and late tail.
2. Real native FX catalog registration / editor and native host power after stable insert lease; source-only JS or false WebView2 render is not sufficient.
3. Audio queue ACK after processing, IMRS v6 round-trip, old v1–v5 fail-closed migration, corrupted state rejection and no per-callback dynamic allocation.
4. Full Windows JUCE EXE + isolated artifact. No release/merge until exact Windows gates and real headphone A/B pass.

**Listening:** Compare 8D-SonKu P9.1–P9.2 and ImerSip at identical *native settings* with no other FX. PCM should match; externalization and bass must not change just because the plugin name changed. Then separately audition ImerSip's scene presets.
