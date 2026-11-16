# ImmerSIB — main-based release-candidate integration gate (17 November 2026)

## Ownership / exact provenance

This is a **selective code forward-port**, not a merge of 400+ experimental
SonKu ancestry commits. The approved **8D-SonKu P9.9 `main` engine** remains
the source of truth. ImmerSIB Engine PR #732 and JUCE GUI/UX PR #722 are
integrated as a **separate product, sharing the one native SonKu PCM core**.

- Baseline `main`: `7b0c2fc1dea359dc98b8807ccd6806ad761ffbd6`.
- GUI source: PR #722, `715b21b882907ba30bc072c26e46fbdf89d003da`.
- Audio source: PR #732, `6a3fe115008329ccc456e15f1448c66ac8b471e8`.
- Donor P9.9 approved 8D-SonKu: `SonKuP99MusicalVocalDepth.h`
  blob `d8542d7aa333d689eea9637fc9f5f122c468277a`.
- No new second HRTF, convolution engine, automatic FFT stem separation,
  per-block heap allocation, binary session ID or graph. Production SonKu
  remains the default owner of its accepted P9.9 orbit/voicing.

## Release-candidate flag and architecture

`-DARMIX_IMMERSIB_RELEASE_CANDIDATE=ON` enables the branded insert and
P9.9 warm vocal/room opt-in only for ImmerSIB, using
`ARMIX_SONKU_APPROVED_P99_MAIN=ON`. The flag defaults **OFF** in normal
builds. This deliberately prevents public binaries from silently shipping
an unapproved headphone voicing.

- Product name: **ImmerSIB**; catalog ID `armix.builtin.imersip.v1`;
  persisted IMRS v6/v7, nested SONK v11/v12 preserved.
- Seven concert presets, Source Distance **1.1–2.5 m**, Early Room
  up to **180%**, Windows-owned floating editor with native photos and
  host-authoritative FX bypass/slot/rack/global power.
- Named presets preserve the full factory floating-point values through
  callback ACK. Only genuine control changes demote to Custom.
- P9.8 Detail/Impact warms body / suppresses intrusive upper mids; P9.9
  room coupling uses **42%/34%** of the approved P9.9 donor profile,
  continuous strength slew, 330 Hz narrow room-band and existing six-wall
  / late-room feeds. ROOM OFF, MIX0, voicing0, private 10 m Venue
  reference and unrelated 8D-SonKu instance must be unaffected.
- GUI work is additive: six 2200×922 venue PNGs; no retired sprite pack,
  fake native power or new independent bypass state.

## Mandatory green release gates

1. Python authority/preset/photo and native bridge source tests.
2. Non-JUCE PCM tests at 44.1 and 48 kHz: Venue P9.7 hash, SonKu P9.9
   donor invariance, non-opted ImmerSIB comparison, 330 Hz body, 86 Hz
   bass, 7.6 kHz air, no NaN, no catastrophic anti-phase and bounded
   linked limiter. Test material is **not** a subjective listening verdict.
3. Authentic Windows JUCE editor/processor state and ACK, all presets
   retaining their label after ACK, edit→Custom, IMRS restoration and
   power readback from slot/rack/global gates.
4. Build executable on Windows from **the same exact integration commit**
   with RC flag ON; package and hash-check portable ZIP and verify
   DPI images at 100/125/150/200%.
5. Default public build with RC flag OFF still passes existing 8D-SonKu
   P9.9 and mixer native regression suites, and does not expose ImmerSIB.
6. Owner headphone listening FAT: level-match Live Concert, Concert Front
   Row XL and SonKu P9.9 on actual mastered concert. Veto thin/cempreng
   vocals, late-room smearing, artificial detached voice, pumping or LF
   impact loss. No substitute for owner acceptance.
7. Release control must create **one** intentional release run from
   a pinned approved `main` SHA, avoid duplicate concurrent release
   jobs, and publish user-facing downloads only after all gates above.
   This integration PR does **not** trigger or enable a public release.

## Rollback

If any gate regresses, disable `ARMIX_IMMERSIB_RELEASE_CANDIDATE`
or revert the isolated integration commit. Neither action must alter the
existing approved 8D-SonKu P9.9 output, DSP state or session format.

**Release status:** CODE INTEGRATION CANDIDATE; NOT PUBLICLY QUALIFIED;
operator headphone FAT and exact release artifact/installer approval pending.
